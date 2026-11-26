/**
 * \file            test_transport_admission.cpp
 * \brief           Two-endpoint reliable admission and HELLO lease contracts
 * \author          X-Gen Lab
 */

#include "test_host_allocator.h"
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>

#include <gtest/gtest.h>

namespace {

class XglTransportAdmissionIntegrationTest : public ::testing::Test {
  protected:
    xgl_transport_ctx_t sender = {};
    xgl_transport_ctx_t receiver = {};
    xgl_layer_stats_t sender_stats = {};
    xgl_layer_stats_t receiver_stats = {};
    xgl_packet_tx_endpoint_t sender_endpoint = {};
    xgl_packet_tx_endpoint_t receiver_endpoint = {};
    bool sender_initialized = false;
    bool receiver_initialized = false;
    bool drop_data = false;
    xgl_error_t data_submission = XGL_OK;
    xgl_error_t application_admission = XGL_OK;
    xgl_error_t hello_admission = XGL_OK;
    xgl_error_t data_admission = XGL_OK;
    unsigned hello_calls = 0U;
    unsigned data_calls = 0U;
    uint8_t payload = 0x42U;

    static xgl_error_t Transmit(void* context, xgl_handle_t,
                                xgl_packet_t* packet) {
        auto& self =
            *static_cast<XglTransportAdmissionIntegrationTest*>(context);
        if (packet->packet_type == XGL_PACKET_TYPE_CONTROL) {
            ++self.hello_calls;
            self.hello_admission =
                xgl_transport_receive(&self.receiver, nullptr, packet);
        } else if (packet->packet_type == XGL_PACKET_TYPE_DATA) {
            ++self.data_calls;
            if (self.data_submission != XGL_OK) {
                return self.data_submission;
            }
            if (!self.drop_data) {
                self.data_admission =
                    xgl_transport_receive(&self.receiver, nullptr, packet);
            }
        }
        /* A PHY accepted submission cannot report the remote protocol result.
         * Feedback is deliberately lost instead of reentering the sender. */
        return XGL_OK;
    }

    static xgl_error_t DiscardFeedback(void*, xgl_handle_t, xgl_packet_t*) {
        return XGL_OK;
    }

    static xgl_error_t Accept(xgl_handle_t, uint16_t, uint8_t, const uint8_t*,
                              size_t, void* context) {
        return static_cast<XglTransportAdmissionIntegrationTest*>(context)
            ->application_admission;
    }

    void Init(uint16_t peer_capacity = 1U) {
        xgl_packet_tx_endpoint_init(&sender_endpoint, this, Transmit);
        xgl_packet_tx_endpoint_init(&receiver_endpoint, nullptr,
                                    DiscardFeedback);
        xgl_transport_config_t config = {};
        config.local_id = 1U;
        config.window_size = 1U;
        config.max_peers = peer_capacity;
        config.max_tx_packets = 1U;
        config.max_retry_count = 1U;
        config.max_frame_size = 128U;
        config.default_timeout_ms = 10000U;
        config.peer_idle_timeout_ms = 0U;
        config.allocator = xgl_test_host_allocator();
        config.lower_layer = &sender_endpoint;
        config.stats = &sender_stats;
        ASSERT_EQ(xgl_transport_init(&sender, &config), XGL_OK);
        sender_initialized = true;
        config.local_id = 2U;
        config.lower_layer = &receiver_endpoint;
        config.stats = &receiver_stats;
        config.rx_accept_callback = Accept;
        config.callback_user_data = this;
        ASSERT_EQ(xgl_transport_init(&receiver, &config), XGL_OK);
        receiver_initialized = true;
    }

    void TearDown() override {
        if (sender_initialized) {
            xgl_transport_destroy(&sender);
        }
        if (receiver_initialized) {
            xgl_transport_destroy(&receiver);
        }
    }

    xgl_error_t Send(uint32_t connection) {
        xgl_tx_data_t request = {};
        request.target_id = 2U;
        request.connection_id = connection;
        request.data = &payload;
        request.data_len = 1U;
        request.reliable = true;
        return xgl_transport_send(&sender, nullptr, &request);
    }

    void Acknowledge(uint32_t connection) {
        uint8_t value[16] = {}, extension[32] = {};
        size_t value_len = 0U, extension_len = 0U;
        const xgl_wire_ack_range_t range = {0U, 1U};
        ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(
                      value, sizeof(value), 0U, 0U, &range, 1U, &value_len),
                  XGL_OK);
        ASSERT_EQ(xgl_wire_encode_ext(extension, sizeof(extension),
                                      XGL_WIRE_EXT_ACK_RANGE, value, value_len,
                                      &extension_len),
                  XGL_OK);
        xgl_packet_t packet = {};
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.connection_id = connection;
        packet.packet_type = XGL_PACKET_TYPE_ACK;
        packet.reliable = XGL_RELIABILITY_ACK_ONLY;
        packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
        packet.extensions = extension;
        packet.extensions_len = extension_len;
        ASSERT_EQ(xgl_transport_receive(&sender, nullptr, &packet), XGL_OK);
    }

    void ExpectRemoteAccepted(uint32_t connection) {
        EXPECT_EQ(hello_admission, XGL_OK);
        EXPECT_EQ(data_admission, XGL_OK);
        auto* peer = transport_find_peer_scope(&receiver, 1U, connection, 0U);
        ASSERT_NE(peer, nullptr);
        EXPECT_TRUE(peer->rx_has_packet_number_state);
        EXPECT_EQ(peer->rx_next_packet_number, 1U);
    }
};

TEST_F(XglTransportAdmissionIntegrationTest,
       CapacityRefusalDoesNotConsumeRemotePeerReservation) {
    Init(2U);
    ASSERT_EQ(Send(9U), XGL_OK);
    ExpectRemoteAccepted(9U);
    EXPECT_EQ(Send(10U), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(hello_calls, 1U);
    EXPECT_EQ(data_calls, 1U);
    EXPECT_EQ(transport_find_peer_scope(&receiver, 1U, 10U, 0U), nullptr);
    Acknowledge(9U);
    ASSERT_EQ(Send(11U), XGL_OK);
    ExpectRemoteAccepted(11U);
    EXPECT_EQ(hello_calls, 2U);
    EXPECT_EQ(data_calls, 2U);
}

TEST_F(XglTransportAdmissionIntegrationTest,
       LostDataAfterHelloReleasesRemoteReservationBeforeNextConnection) {
    Init();
    drop_data = true;
    ASSERT_EQ(Send(10U), XGL_OK);
    auto* empty = transport_find_peer_scope(&receiver, 1U, 10U, 0U);
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(empty->rx_has_packet_number_state);
    ASSERT_EQ(xgl_transport_run(&receiver, nullptr, 4999U), XGL_OK);
    EXPECT_EQ(receiver.peers, empty);
    ASSERT_EQ(xgl_transport_run(&receiver, nullptr, 5000U), XGL_OK);
    EXPECT_EQ(receiver.peers, nullptr);
    ASSERT_EQ(xgl_transport_close_scope(&sender, nullptr, 2U, 10U, 0U), XGL_OK);
    drop_data = false;
    ASSERT_EQ(Send(11U), XGL_OK);
    ExpectRemoteAccepted(11U);
}

TEST_F(XglTransportAdmissionIntegrationTest,
       RejectedDataSubmissionLeavesOnlyBoundedRemoteHelloReservation) {
    Init();
    data_submission = XGL_ERR_BUSY;
    EXPECT_EQ(Send(10U), XGL_ERR_BUSY);
    EXPECT_EQ(sender.peers, nullptr);
    auto* empty = transport_find_peer_scope(&receiver, 1U, 10U, 0U);
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(empty->rx_has_packet_number_state);
    ASSERT_EQ(xgl_transport_run(&receiver, nullptr, 5000U), XGL_OK);
    EXPECT_EQ(receiver.peers, nullptr);
    data_submission = XGL_OK;
    ASSERT_EQ(Send(11U), XGL_OK);
    ExpectRemoteAccepted(11U);
}

TEST_F(XglTransportAdmissionIntegrationTest,
       RejectedApplicationDataDoesNotRenewHelloLease) {
    Init();
    application_admission = XGL_ERR_BUSY;
    ASSERT_EQ(Send(10U), XGL_OK);
    EXPECT_EQ(data_admission, XGL_ERR_BUSY);
    auto* empty = transport_find_peer_scope(&receiver, 1U, 10U, 0U);
    ASSERT_NE(empty, nullptr);
    EXPECT_FALSE(empty->rx_has_packet_number_state);
    receiver.current_time_ms = 4900U;
    ASSERT_EQ(xgl_transport_run(&sender, nullptr, 10000U), XGL_OK);
    EXPECT_EQ(data_admission, XGL_ERR_BUSY);
    EXPECT_EQ(xgl_transport_run(&receiver, nullptr, 5000U), XGL_OK);
    EXPECT_EQ(receiver.peers, nullptr);
}

}  // namespace
