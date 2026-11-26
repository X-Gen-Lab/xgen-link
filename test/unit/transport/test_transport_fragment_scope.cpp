/**
 * \file            test_transport_fragment_scope.cpp
 * \brief           Fragment reliability, timeout and scope ownership contracts
 */

#include "test_host_allocator.h"
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>

#include <array>
#include <gtest/gtest.h>
#include <vector>

#if XGL_FEATURE_FRAGMENTATION
namespace {

class XglTransportFragmentScopeTest : public ::testing::Test {
  protected:
    xgl_transport_ctx_t context = {};
    xgl_layer_stats_t statistics = {};
    xgl_packet_tx_endpoint_t lower = {};
    xgl_error_t feedback_error = XGL_OK;
    size_t feedback_attempts = 0U;
    std::vector<uint8_t> delivered;
    std::vector<xgl_error_t> errors;

    static xgl_error_t SendPacket(void* opaque, xgl_handle_t,
                                  xgl_packet_t* packet) {
        auto& self = *static_cast<XglTransportFragmentScopeTest*>(opaque);
        if (packet->packet_type == XGL_PACKET_TYPE_ACK) {
            ++self.feedback_attempts;
            return self.feedback_error;
        }
        return XGL_OK;
    }

    static xgl_error_t Accept(xgl_handle_t, uint16_t, uint8_t,
                              const uint8_t* data, size_t length,
                              void* opaque) {
        auto& self = *static_cast<XglTransportFragmentScopeTest*>(opaque);
        self.delivered.insert(self.delivered.end(), data, data + length);
        return XGL_OK;
    }

    static void Error(xgl_handle_t, xgl_error_t error, const char*,
                      void* opaque) {
        static_cast<XglTransportFragmentScopeTest*>(opaque)->errors.push_back(
            error);
    }

    void SetUp() override {
        lower = {this, SendPacket};
        xgl_transport_config_t config = {};
        config.allocator = xgl_test_host_allocator();
        config.local_id = 1U;
        config.max_peers = 3U;
        config.max_tx_packets = 4U;
        config.max_rx_buffered_packets = 4U;
        config.max_retry_count = 3U;
        config.default_timeout_ms = 10000U;
        config.window_size = 4U;
        config.max_frame_size = 128U;
        config.lower_layer = &lower;
        config.stats = &statistics;
        config.rx_accept_callback = Accept;
        config.error_callback = Error;
        config.callback_user_data = this;
        config.enable_fragmentation = true;
        config.max_message_size = 128U;
        config.max_reassembly_slots = 4U;
        config.max_reassembly_bytes = 512U;
        config.max_tx_message_bytes = 128U;
        ASSERT_EQ(xgl_transport_init(&context, &config), XGL_OK);
    }

    void TearDown() override {
        xgl_transport_destroy(&context);
    }

    xgl_transport_peer_state_t* Peer(uint32_t connection = 10U) {
        return transport_find_peer_scope(&context, 2U, connection, 9U);
    }

    xgl_error_t SendReliable(uint32_t connection = 10U) {
        const uint8_t payload = 0x5AU;
        xgl_tx_data_t request = {};
        request.target_id = 2U;
        request.connection_id = connection;
        request.session_epoch = 9U;
        request.reliable = true;
        request.timeout_ms = 10000U;
        request.data = &payload;
        request.data_len = 1U;
        return xgl_transport_send(&context, nullptr, &request);
    }

    xgl_error_t Receive(uint32_t number, bool reliable, bool fragmented,
                        uint32_t message = 7U, uint32_t offset = 0U,
                        uint32_t total = 2U, uint32_t connection = 10U) {
        std::array<uint8_t, XGL_FRAGMENT_EXT_VALUE_SIZE> value = {};
        std::array<uint8_t, XGL_FRAGMENT_EXT_VALUE_SIZE + 2U> extensions = {};
        size_t value_length = 0U, extensions_length = 0U;
        if (fragmented) {
            xgl_error_t error = xgl_wire_encode_fragment_ext_value(
                value.data(), value.size(), message, offset, total,
                &value_length);
            if (error != XGL_OK) {
                return error;
            }
            error = xgl_wire_encode_ext(extensions.data(), extensions.size(),
                                        XGL_WIRE_EXT_FRAGMENT, value.data(),
                                        value_length, &extensions_length);
            if (error != XGL_OK) {
                return error;
            }
        }
        const uint8_t payload = static_cast<uint8_t>(offset + 0x40U);
        xgl_packet_data_t data = {1U, &payload};
        xgl_packet_t packet = {};
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.connection_id = connection;
        packet.session_epoch = 9U;
        packet.packet_number = number;
        packet.packet_type = XGL_PACKET_TYPE_DATA;
        packet.reliable =
            reliable ? XGL_RELIABILITY_ACK_ELICITING : XGL_RELIABILITY_NONE;
        packet.fragment = fragmented;
        packet.data = &data;
        packet.extensions = fragmented ? extensions.data() : nullptr;
        packet.extensions_len = extensions_length;
        return xgl_transport_receive(&context, nullptr, &packet);
    }
};

TEST_F(XglTransportFragmentScopeTest,
       UnreliableTimeoutPreservesReliableHistoryAndPendingTx) {
    ASSERT_EQ(SendReliable(), XGL_OK);
    ASSERT_EQ(Receive(0U, true, false), XGL_OK);
    ASSERT_EQ(Receive(0U, false, true), XGL_OK);
    ASSERT_NE(Peer(), nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&Peer()->reliable_queue), 1U);

    EXPECT_EQ(xgl_transport_run(&context, nullptr, XGL_FRAGMENT_TIMEOUT_MS),
              XGL_ERR_TIMEOUT);
    ASSERT_NE(Peer(), nullptr);
    EXPECT_FALSE(Peer()->failed);
    EXPECT_EQ(xgl_reliable_get_count(&Peer()->reliable_queue), 1U);
    EXPECT_EQ(Peer()->rx_next_packet_number, 1U);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 0U);
    EXPECT_EQ(errors, std::vector<xgl_error_t>({XGL_ERR_TIMEOUT}));
}

TEST_F(XglTransportFragmentScopeTest,
       UnreliableTimeoutPreservesScopeWithoutReliableHistory) {
    ASSERT_EQ(SendReliable(), XGL_OK);
    ASSERT_EQ(Receive(0U, false, true), XGL_OK);
    EXPECT_EQ(xgl_transport_run(&context, nullptr, XGL_FRAGMENT_TIMEOUT_MS),
              XGL_ERR_TIMEOUT);
    ASSERT_NE(Peer(), nullptr);
    EXPECT_FALSE(Peer()->failed);
    EXPECT_EQ(xgl_reliable_get_count(&Peer()->reliable_queue), 1U);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 0U);
}

TEST_F(XglTransportFragmentScopeTest,
       ReliableTimeoutRemainsTerminalAfterFeedbackBusy) {
    ASSERT_EQ(SendReliable(), XGL_OK);
    feedback_error = XGL_ERR_BUSY;
    ASSERT_EQ(Receive(0U, true, true), XGL_ERR_BUSY);
    ASSERT_NE(Peer(), nullptr);
    ASSERT_EQ(Peer()->rx_next_packet_number, 1U);

    EXPECT_EQ(xgl_transport_run(&context, nullptr, XGL_FRAGMENT_TIMEOUT_MS),
              XGL_ERR_TIMEOUT);
    EXPECT_TRUE(Peer()->failed);
    EXPECT_EQ(xgl_reliable_get_count(&Peer()->reliable_queue), 0U);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 0U);
    EXPECT_EQ(errors, std::vector<xgl_error_t>({XGL_ERR_TIMEOUT}));
}

TEST_F(XglTransportFragmentScopeTest,
       RejectsUnreliableTailOfReliableMessageWithoutChangingOwnedBytes) {
    ASSERT_EQ(Receive(0U, true, true), XGL_OK);
    const size_t feedback_before = feedback_attempts;
    EXPECT_EQ(Receive(0U, false, true, 7U, 1U), XGL_ERR_INVALID_FRAME);
    EXPECT_TRUE(delivered.empty());
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 2U);
    EXPECT_EQ(feedback_attempts, feedback_before);

    ASSERT_EQ(Receive(1U, true, true, 7U, 1U), XGL_OK);
    EXPECT_EQ(delivered, std::vector<uint8_t>({0x40U, 0x41U}));
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 0U);
}

TEST_F(XglTransportFragmentScopeTest,
       RejectsReliableTailOfUnreliableMessageBeforeAck) {
    ASSERT_EQ(Receive(0U, false, true), XGL_OK);
    EXPECT_EQ(Receive(0U, true, true, 7U, 1U), XGL_ERR_INVALID_FRAME);
    EXPECT_TRUE(delivered.empty());
    EXPECT_EQ(feedback_attempts, 0U);
    EXPECT_FALSE(Peer()->rx_has_packet_number_state);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 2U);

    ASSERT_EQ(Receive(0U, false, true, 7U, 1U), XGL_OK);
    EXPECT_EQ(delivered, std::vector<uint8_t>({0x40U, 0x41U}));
}

#if XGL_FEATURE_OUT_OF_ORDER
TEST_F(XglTransportFragmentScopeTest,
       RejectsReliableOutOfOrderTailOfUnreliableMessageBeforeSack) {
    ASSERT_EQ(Receive(0U, false, true), XGL_OK);
    EXPECT_EQ(Receive(1U, true, true, 7U, 1U), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(feedback_attempts, 0U);
    EXPECT_EQ(Peer()->rx_buffered_count, 0U);
    EXPECT_FALSE(Peer()->rx_has_packet_number_state);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 2U);
}

TEST_F(XglTransportFragmentScopeTest,
       RejectsUnreliableMessageMatchingOwnedReliableOutOfOrderFragment) {
    ASSERT_EQ(Receive(1U, true, true, 7U, 1U), XGL_OK);
    ASSERT_EQ(Peer()->rx_buffered_count, 1U);
    const size_t feedback_before = feedback_attempts;
    EXPECT_EQ(Receive(0U, false, true), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 0U);
    EXPECT_EQ(Peer()->rx_buffered_count, 1U);
    EXPECT_EQ(feedback_attempts, feedback_before);
    EXPECT_TRUE(delivered.empty());

    ASSERT_EQ(Receive(0U, true, true), XGL_OK);
    EXPECT_EQ(delivered, std::vector<uint8_t>({0x40U, 0x41U}));
    EXPECT_EQ(Peer()->rx_buffered_count, 0U);
}
#endif

TEST_F(XglTransportFragmentScopeTest,
       ExpiredMessagesClearEachOwningScopeAndContinueOtherScopes) {
    ASSERT_EQ(SendReliable(), XGL_OK);
    ASSERT_EQ(SendReliable(11U), XGL_OK);
    ASSERT_EQ(Receive(0U, true, true, 7U, 0U, 3U), XGL_OK);
    ASSERT_EQ(Receive(1U, true, true, 8U, 0U, 3U), XGL_OK);
    ASSERT_EQ(Receive(0U, true, true, 9U, 0U, 3U, 11U), XGL_OK);
    ASSERT_EQ(Receive(0U, false, true, 10U, 0U, 3U, 12U), XGL_OK);
    ASSERT_EQ(xgl_fragment_get_reassembly_count(context.fragment_mgr), 4U);

    EXPECT_EQ(xgl_transport_run(&context, nullptr, XGL_FRAGMENT_TIMEOUT_MS),
              XGL_ERR_TIMEOUT);
    ASSERT_NE(Peer(), nullptr);
    ASSERT_NE(Peer(11U), nullptr);
    ASSERT_NE(Peer(12U), nullptr);
    EXPECT_TRUE(Peer()->failed);
    EXPECT_TRUE(Peer(11U)->failed);
    EXPECT_FALSE(Peer(12U)->failed);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 0U);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(context.fragment_mgr), 0U);
    EXPECT_EQ(errors.size(), 3U);
}

}  // namespace
#endif
