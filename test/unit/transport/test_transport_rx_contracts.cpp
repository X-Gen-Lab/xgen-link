/**
 * \file            test_transport_rx_contracts.cpp
 * \brief           Receive admission and acknowledgement failure contracts
 * \author          X-Gen Lab
 */

#include "test_host_allocator.h"
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_frame.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

class XglTransportRxContract : public ::testing::Test {
  protected:
    xgl_transport_ctx_t context = {};
    xgl_layer_stats_t statistics = {};
    uint64_t retries = 0U;
    xgl_packet_tx_endpoint_t lower = {};
    xgl_error_t feedback_error = XGL_OK;
    xgl_error_t application_error = XGL_OK;
    size_t feedback_attempts = 0U;
    std::vector<uint8_t> delivered;
    std::vector<xgl_error_t> errors;
    bool initialized = false;

    static xgl_error_t Send(void* opaque, xgl_handle_t, xgl_packet_t*) {
        auto& self = *static_cast<XglTransportRxContract*>(opaque);
        ++self.feedback_attempts;
        return self.feedback_error;
    }

    static xgl_error_t Accept(xgl_handle_t, uint16_t, uint8_t,
                              const uint8_t* data, size_t len, void* opaque) {
        auto& self = *static_cast<XglTransportRxContract*>(opaque);
        if (self.application_error == XGL_OK) {
            self.delivered.insert(self.delivered.end(), data, data + len);
        }
        return self.application_error;
    }

    static void ReportError(xgl_handle_t, xgl_error_t error, const char*,
                            void* opaque) {
        static_cast<XglTransportRxContract*>(opaque)->errors.push_back(error);
    }

    void SetUp() override {
        lower = {this, Send};
        xgl_transport_config_t config = {};
        config.allocator = xgl_test_host_allocator();
        config.max_peers = 1U;
        config.max_tx_packets = 4U;
        config.max_rx_buffered_packets = 4U;
        config.local_id = 1U;
        config.max_retry_count = 3U;
        config.default_timeout_ms = 100U;
        config.window_size = 4U;
        config.max_frame_size = 128U;
        config.lower_layer = &lower;
        config.stats = &statistics;
        config.tx_retries = &retries;
        config.rx_accept_callback = Accept;
        config.error_callback = ReportError;
        config.callback_user_data = this;
#if XGL_FEATURE_FRAGMENTATION
        config.enable_fragmentation = true;
        config.max_message_size = 128U;
        config.max_reassembly_slots = 2U;
        config.max_reassembly_bytes = 256U;
        config.max_tx_message_bytes = 128U;
#endif
        ASSERT_EQ(xgl_transport_init(&context, &config), XGL_OK);
        initialized = true;
    }

    void TearDown() override {
        if (initialized) {
            xgl_transport_destroy(&context);
        }
    }

    xgl_error_t Receive(uint32_t number, uint8_t value,
                        const std::vector<uint8_t>& extensions = {},
                        bool fragmented = false) {
        xgl_packet_data_t data = {1U, &value};
        xgl_packet_t packet = {};
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.packet_number = number;
        packet.packet_type = XGL_PACKET_TYPE_DATA;
        packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
        packet.fragment = fragmented;
        packet.data = &data;
        packet.extensions = extensions.empty() ? nullptr : extensions.data();
        packet.extensions_len = extensions.size();
        return xgl_transport_receive(&context, nullptr, &packet);
    }
};

struct PublicReceiver {
    std::vector<uint8_t> input;
    size_t cursor = 0U;
    size_t delivered = 0U;
    size_t feedback_attempts = 0U;
    uint32_t now_ms = 0U;
    xgl_error_t feedback_error = XGL_OK;

    static xgl_error_t Read(uint8_t* output, size_t* len, void* opaque) {
        auto& self = *static_cast<PublicReceiver*>(opaque);
        *len = std::min(*len, self.input.size() - self.cursor);
        if (*len > 0U) {
            std::memcpy(output, self.input.data() + self.cursor, *len);
            self.cursor += *len;
        }
        return XGL_OK;
    }

    static xgl_error_t Write(const uint8_t*, size_t, void* opaque) {
        auto& self = *static_cast<PublicReceiver*>(opaque);
        ++self.feedback_attempts;
        return self.feedback_error;
    }

    static xgl_error_t Accept(xgl_handle_t, uint16_t, uint8_t, const uint8_t*,
                              size_t, void* opaque) {
        ++static_cast<PublicReceiver*>(opaque)->delivered;
        return XGL_OK;
    }

    xgl_phy_ops_t phy = {Write, Read, this};
    xgl_route_item_t route = {2U, &phy, 128U, 1000U, 0U};
    xgl_config_t config = {};
    std::unique_ptr<xgl_instance, decltype(&xgl_destroy)> instance{
        nullptr, &xgl_destroy};

    void Init(bool with_handler = true) {
        xgl_config_get_default(&config);
        config.memory.allocator = xgl_test_host_allocator();
        config.protocol.max_frame_size = 128U;
        config.memory.rx_buffer_size = 128U;
        config.route_table = &route;
        config.route_table_len = 1U;
        config.rx_accept_callback = with_handler ? Accept : nullptr;
        config.callback_user_data = this;
        ASSERT_EQ(xgl_config_validate(&config), XGL_OK);
        instance.reset(xgl_create(&config));
        ASSERT_NE(instance, nullptr);
        ASSERT_EQ(xgl_init(instance.get()), XGL_OK);
    }

    xgl_error_t Feed(const std::vector<uint8_t>& frame) {
        input = frame;
        cursor = 0U;
        xgl_error_t first_error = XGL_OK;
        const xgl_work_budget_t budget = {128U, 1000U};
        for (unsigned attempt = 0U; cursor < input.size() && attempt < 100U;
             ++attempt) {
            xgl_error_t error = xgl_step(instance.get(), now_ms++, &budget);
            if (first_error == XGL_OK) {
                first_error = error;
            }
        }
        EXPECT_EQ(cursor, input.size());
        return first_error;
    }
};

std::vector<uint8_t> MakeFrame(uint8_t type,
                               const std::vector<uint8_t>& extensions = {},
                               bool fragmented = false, bool reliable = false,
                               uint16_t target = 1U) {
    const uint8_t payload = 0x42U;
    xgl_frame_params_t parameters = {};
    parameters.source_id = 2U;
    parameters.target_id = target;
    parameters.packet_type = type;
    parameters.reliable =
        reliable ? XGL_RELIABILITY_ACK_ELICITING : XGL_RELIABILITY_NONE;
    parameters.fragment = fragmented;
    parameters.extensions = extensions.empty() ? nullptr : extensions.data();
    parameters.extensions_len = extensions.size();
    parameters.payload = &payload;
    parameters.payload_len = 1U;
    parameters.ttl = 4U;
    xgl_frame_t frame = {};
    EXPECT_EQ(xgl_frame_build(&frame, &parameters), XGL_OK);
    std::vector<uint8_t> encoded(128U);
    size_t size = 0U;
    EXPECT_EQ(
        xgl_frame_serialize(encoded.data(), encoded.size(), &frame, &size),
        XGL_OK);
    encoded.resize(size);
    return encoded;
}

#if XGL_FEATURE_FRAGMENTATION
std::vector<uint8_t> Fragment(uint32_t message = 7U, uint32_t offset = 0U,
                              uint32_t total = 1U) {
    std::array<uint8_t, XGL_FRAGMENT_EXT_VALUE_SIZE> value = {};
    size_t value_len = 0U, encoded_len = 0U;
    EXPECT_EQ(xgl_wire_encode_fragment_ext_value(value.data(), value.size(),
                                                 message, offset, total,
                                                 &value_len),
              XGL_OK);
    std::vector<uint8_t> encoded(value.size() + 2U);
    EXPECT_EQ(xgl_wire_encode_ext(encoded.data(), encoded.size(),
                                  XGL_WIRE_EXT_FRAGMENT, value.data(),
                                  value_len, &encoded_len),
              XGL_OK);
    encoded.resize(encoded_len);
    return encoded;
}

std::vector<uint8_t> DuplicateFragments() {
    auto extensions = Fragment();
    auto duplicate = Fragment(8U);
    extensions.insert(extensions.end(), duplicate.begin(), duplicate.end());
    return extensions;
}
#endif

}  // namespace

TEST(XglTransportRxWireContract, ReservedPacketTypesNeverReachApplication) {
    PublicReceiver receiver;
    receiver.Init();
    for (uint8_t type : {uint8_t{4U}, uint8_t{5U}, uint8_t{6U}, uint8_t{7U}}) {
        SCOPED_TRACE(static_cast<unsigned>(type));
        auto frame = MakeFrame(type);
        ASSERT_FALSE(frame.empty());
        EXPECT_EQ(receiver.Feed(frame), XGL_ERR_INVALID_FRAME);
    }
    EXPECT_EQ(receiver.delivered, 0U);
    EXPECT_EQ(receiver.feedback_attempts, 0U);
}

TEST(XglTransportRxWireContract, ReliableBroadcastPreservesUnicastHistory) {
    PublicReceiver receiver;
    receiver.Init();
    auto broadcast =
        MakeFrame(XGL_PACKET_TYPE_DATA, {}, false, true, UINT16_MAX);
    ASSERT_FALSE(broadcast.empty());
    EXPECT_EQ(receiver.Feed(broadcast), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(receiver.delivered, 0U);
    EXPECT_EQ(receiver.feedback_attempts, 0U);

    auto unicast = MakeFrame(XGL_PACKET_TYPE_DATA, {}, false, true);
    ASSERT_FALSE(unicast.empty());
    EXPECT_EQ(receiver.Feed(unicast), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 1U);
}

TEST(XglTransportRxWireContract, UnreliableBroadcastStillDelivers) {
    PublicReceiver receiver;
    receiver.Init();
    auto frame = MakeFrame(XGL_PACKET_TYPE_DATA, {}, false, false, UINT16_MAX);
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(receiver.Feed(frame), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 0U);
}

TEST(XglTransportRxWireContract, StepReportsFeedbackFailureWithoutRedelivery) {
    PublicReceiver receiver;
    receiver.Init();
    auto frame = MakeFrame(XGL_PACKET_TYPE_DATA, {}, false, true);
    ASSERT_FALSE(frame.empty());
    receiver.feedback_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(receiver.Feed(frame), XGL_ERR_TX_FAILED);
    EXPECT_EQ(receiver.delivered, 1U);
    receiver.feedback_error = XGL_OK;
    EXPECT_EQ(receiver.Feed(frame), XGL_OK);
    EXPECT_EQ(receiver.delivered, 1U);
    EXPECT_EQ(receiver.feedback_attempts, 2U);
}

TEST(XglTransportRxWireContract, MissingHandlerCannotAcknowledgeData) {
    PublicReceiver receiver;
    receiver.Init(false);
    const auto frame = MakeFrame(XGL_PACKET_TYPE_DATA, {}, false, true);
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(receiver.Feed(frame), XGL_ERR_UNSUPPORTED);
    EXPECT_EQ(receiver.feedback_attempts, 0U);
#if XGL_FEATURE_STATISTICS
    xgl_statistics_t statistics = {};
    ASSERT_EQ(xgl_stats_get(receiver.instance.get(), &statistics), XGL_OK);
    EXPECT_EQ(statistics.transport.rx_packets, 0U);
    EXPECT_EQ(statistics.transport.rx_bytes, 0U);
    EXPECT_EQ(statistics.transport.rx_dropped, 1U);
#endif
}

#if XGL_BUILD_WINDOW_CAPACITY >= 4
TEST_F(XglTransportRxContract, ReservedNumberCannotCreateReceiveHistory) {
    EXPECT_EQ(Receive(UINT32_MAX, 0x41U), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(context.peers, nullptr);
    EXPECT_EQ(feedback_attempts, 0U);
    EXPECT_TRUE(delivered.empty());
}

TEST_F(XglTransportRxContract, ExhaustedNumberCannotWrapOrRedeliverOldData) {
    ASSERT_EQ(Receive(0U, 0x41U), XGL_OK);
    ASSERT_NE(context.peers, nullptr);
    /* Model the reachable boundary after earlier numbers were accepted. */
    context.peers->rx_next_packet_number = UINT32_MAX - 1U;
    ASSERT_EQ(Receive(UINT32_MAX - 1U, 0x42U), XGL_OK);
    const auto attempts_before = feedback_attempts;
    EXPECT_EQ(Receive(UINT32_MAX, 0x43U), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(context.peers->rx_next_packet_number, UINT32_MAX);
    EXPECT_EQ(feedback_attempts, attempts_before);
    EXPECT_EQ(Receive(0U, 0x41U), XGL_OK);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U, 0x42U}));
}

TEST_F(XglTransportRxContract, FeedbackFailureKeepsAcceptedHistory) {
    feedback_error = XGL_ERR_BUSY;
    EXPECT_EQ(Receive(0U, 0x41U), XGL_ERR_BUSY);
    ASSERT_NE(context.peers, nullptr);
    EXPECT_EQ(context.peers->rx_next_packet_number, 1U);
    EXPECT_FALSE(context.peers->failed);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U}));
    EXPECT_EQ(Receive(0U, 0x41U), XGL_ERR_BUSY);
    EXPECT_EQ(delivered.size(), 1U);
    feedback_error = XGL_OK;
    EXPECT_EQ(Receive(0U, 0x41U), XGL_OK);
    EXPECT_EQ(delivered.size(), 1U);
}
#endif

#if XGL_FEATURE_OUT_OF_ORDER
TEST_F(XglTransportRxContract, SackFailureRetainsOwnedPacket) {
    feedback_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(Receive(1U, 0x42U), XGL_ERR_TX_FAILED);
    ASSERT_NE(context.peers, nullptr);
    EXPECT_EQ(context.peers->rx_buffered_count, 1U);
    EXPECT_TRUE(context.peers->rx_has_packet_number_state);
    feedback_error = XGL_OK;
    EXPECT_EQ(Receive(0U, 0x41U), XGL_OK);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U, 0x42U}));
    EXPECT_EQ(context.peers->rx_buffered_count, 0U);
}

TEST_F(XglTransportRxContract, FeedbackFailureStillDrainsContiguousOwnedData) {
    ASSERT_EQ(Receive(1U, 0x42U), XGL_OK);
    ASSERT_EQ(Receive(2U, 0x43U), XGL_OK);
    feedback_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(Receive(0U, 0x41U), XGL_ERR_TX_FAILED);
    ASSERT_NE(context.peers, nullptr);
    EXPECT_EQ(context.peers->rx_next_packet_number, 3U);
    EXPECT_EQ(context.peers->rx_buffered_count, 0U);
    EXPECT_FALSE(context.peers->failed);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U, 0x42U, 0x43U}));
}

TEST_F(XglTransportRxContract, AdmissionErrorPrecedesFeedbackError) {
    context.max_rx_buffered_packets = 1U;
    ASSERT_EQ(Receive(1U, 0x42U), XGL_OK);
    feedback_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(Receive(2U, 0x43U), XGL_ERR_WINDOW_FULL);
    ASSERT_NE(context.peers, nullptr);
    EXPECT_EQ(context.peers->rx_buffered_count, 1U);
}
#endif

#if XGL_FEATURE_FRAGMENTATION
TEST_F(XglTransportRxContract, PendingMessageFailureReleasesOwnedBytesOnce) {
    application_error = XGL_ERR_BUSY;
    ASSERT_EQ(Receive(0U, 0x41U, Fragment(), true), XGL_OK);
    ASSERT_NE(context.peers, nullptr);
    auto* peer = context.peers;
    ASSERT_NE(peer->rx_pending_message.data, nullptr);
    const auto* owned = peer->rx_pending_message.data;
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 1U);
    EXPECT_EQ(peer->rx_next_packet_number, 1U);
    EXPECT_EQ(feedback_attempts, 1U);

    EXPECT_EQ(xgl_transport_run(&context, nullptr, 10U), XGL_OK);
    EXPECT_EQ(peer->rx_pending_message.data, owned);
    application_error = XGL_ERR_NO_MEMORY;
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 11U), XGL_OK);
    EXPECT_EQ(peer->rx_pending_message.data, owned);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 1U);
    EXPECT_TRUE(errors.empty());

    application_error = XGL_ERR_INVALID_PARAM;
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 12U), XGL_ERR_INVALID_PARAM);
    EXPECT_TRUE(peer->failed);
    EXPECT_EQ(peer->rx_pending_message.data, nullptr);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 0U);
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_INVALID_PARAM}));
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 13U), XGL_OK);
    EXPECT_EQ(errors.size(), 1U);
    EXPECT_EQ(feedback_attempts, 1U);
    EXPECT_TRUE(delivered.empty());
}

TEST_F(XglTransportRxContract, MissingHandlerCannotReserveFragmentStorage) {
    context.rx_accept_callback = nullptr;
    EXPECT_EQ(Receive(0U, 0x41U, Fragment(7U, 0U, 2U), true),
              XGL_ERR_UNSUPPORTED);
    EXPECT_EQ(context.peers, nullptr);
    EXPECT_EQ(context.fragment_mgr->current_reassembly_bytes, 0U);
    EXPECT_EQ(feedback_attempts, 0U);
}

TEST(XglTransportRxWireContract, DuplicateFragmentMetadataNeverDelivers) {
    PublicReceiver receiver;
    receiver.Init();
    auto frame = MakeFrame(XGL_PACKET_TYPE_DATA, DuplicateFragments(), true);
    ASSERT_FALSE(frame.empty());
    EXPECT_EQ(receiver.Feed(frame), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(receiver.delivered, 0U);
}

TEST_F(XglTransportRxContract, UnknownExtensionsRemainCompatibleWithFragment) {
    std::vector<uint8_t> extensions = {0x7EU, 1U, 0xAAU};
    auto fragment = Fragment();
    extensions.insert(extensions.end(), fragment.begin(), fragment.end());
    extensions.insert(extensions.end(), {0x7DU, 1U, 0xBBU});
    EXPECT_EQ(Receive(0U, 0x41U, extensions, true), XGL_OK);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0x41U}));
}

#if XGL_FEATURE_OUT_OF_ORDER
TEST_F(XglTransportRxContract, MissingHandlerCannotCacheFutureData) {
    context.rx_accept_callback = nullptr;
    auto* peer = transport_get_or_create_peer_scope(&context, 2U, 0U, 0U);
    ASSERT_NE(peer, nullptr);
    EXPECT_EQ(Receive(1U, 0x41U), XGL_ERR_UNSUPPORTED);
    EXPECT_EQ(context.peers, peer);
    EXPECT_FALSE(peer->rx_has_packet_number_state);
    EXPECT_EQ(peer->rx_buffered_count, 0U);
    EXPECT_EQ(feedback_attempts, 0U);
}

TEST_F(XglTransportRxContract, DuplicateFragmentRejectsBeforeCacheAndSack) {
    EXPECT_EQ(Receive(1U, 0x41U, DuplicateFragments(), true),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(context.peers, nullptr);
    EXPECT_EQ(feedback_attempts, 0U);
    EXPECT_TRUE(delivered.empty());
}

TEST_F(XglTransportRxContract, MissingFragmentRejectsBeforeCacheAndSack) {
    EXPECT_EQ(Receive(1U, 0x41U, {0x7EU, 1U, 0xAAU}, true),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(context.peers, nullptr);
    EXPECT_EQ(feedback_attempts, 0U);
}

TEST_F(XglTransportRxContract, MalformedFragmentTailRejectsBeforeCacheAndSack) {
    auto extensions = Fragment();
    extensions.insert(extensions.end(), {0x7EU, 3U, 0xAAU});
    EXPECT_EQ(Receive(1U, 0x41U, extensions, true), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(context.peers, nullptr);
    EXPECT_EQ(feedback_attempts, 0U);
}

TEST_F(XglTransportRxContract, InvalidFragmentRangeRejectsBeforeCacheAndSack) {
    EXPECT_EQ(Receive(1U, 0x41U, Fragment(7U, 1U, 1U), true),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(context.peers, nullptr);
    EXPECT_EQ(feedback_attempts, 0U);
}
#endif
#endif
