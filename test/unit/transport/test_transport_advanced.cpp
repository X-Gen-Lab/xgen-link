/**
 * \file            test_transport_advanced.cpp
 * \brief           Optional scoped runtime, deadlines and bounded maintenance
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-02
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_host_allocator.h"
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <vector>

#if XGL_FEATURE_ADVANCED_RUNTIME
namespace {
class XglTransportAdvancedTest : public ::testing::Test {
  protected:
    xgl_transport_ctx_t ctx = {};
    xgl_layer_stats_t stats = {};
    xgl_packet_tx_endpoint_t lower = {};
    std::vector<uint16_t> targets;
    std::vector<uint32_t> submitted_numbers;
    std::vector<xgl_scope_event_t> events;
    std::vector<xgl_error_t> errors;
    std::vector<xgl_rx_info_t> received;
    xgl_error_t submit_result = XGL_OK;
    xgl_error_t accept_result = XGL_OK;
    bool initialized = false;
    size_t submit_attempts = 0U;
    size_t accepted_before_busy = SIZE_MAX;

    static xgl_error_t Submit(void* user, xgl_handle_t, xgl_packet_t* packet) {
        auto& self = *static_cast<XglTransportAdvancedTest*>(user);
        self.submit_attempts++;
        if (packet->packet_type == XGL_PACKET_TYPE_DATA) {
            self.targets.push_back(packet->target_id);
            self.submitted_numbers.push_back(packet->packet_number);
            return self.submit_result;
        }
        return XGL_OK;
    }

    static xgl_error_t Accept(xgl_handle_t, const xgl_rx_info_t* info,
                              const uint8_t*, size_t, void* user) {
        auto& self = *static_cast<XglTransportAdvancedTest*>(user);
        self.received.push_back(*info);
        return self.received.size() > self.accepted_before_busy
                   ? XGL_ERR_BUSY
                   : self.accept_result;
    }

    static void Event(xgl_handle_t, uint16_t, uint32_t, uint32_t,
                      xgl_scope_event_t event, xgl_error_t error, void* user) {
        auto& self = *static_cast<XglTransportAdvancedTest*>(user);
        self.events.push_back(event);
        self.errors.push_back(error);
    }

    void Init(uint32_t lifetime = 0U, uint8_t window = 1U) {
        lower = {this, Submit, nullptr, nullptr};
        xgl_transport_config_t config = {};
        config.local_id = 1U;
        config.max_retry_count = 3U;
        config.default_timeout_ms = 100U;
        config.window_size = window;
        config.max_frame_size = 64U;
        config.max_peers = 3U;
        config.max_tx_packets = 3U;
        config.max_rx_buffered_packets = 3U;
        config.lower_layer = &lower;
        config.stats = &stats;
        config.allocator = xgl_test_host_allocator();
        config.tx_lifetime_ms = lifetime;
        config.rx_scoped_callback = Accept;
        config.scope_event_callback = Event;
        config.extended_user_data = this;
        ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);
        initialized = true;
    }

    void TearDown() override {
        if (initialized)
            xgl_transport_destroy(&ctx);
    }

    xgl_error_t Send(uint16_t target = 2U, size_t length = 1U,
                     bool reliable = true) {
        const std::array<uint8_t, 128> payload = {};
        xgl_tx_data_t data = {};
        data.target_id = target;
        data.connection_id = 11U;
        data.session_epoch = 22U;
        data.reliable = reliable;
        data.data = payload.data();
        data.data_len = length;
        return xgl_transport_send(&ctx, nullptr, &data);
    }

    xgl_error_t Ack(uint16_t source, uint32_t number, bool selective = false) {
        std::array<uint8_t, 32> value = {}, ext = {};
        size_t value_len = 0U, ext_len = 0U;
        const xgl_wire_ack_range_t range = {0U, 1U};
        const uint8_t bitmap = 1U;
        xgl_error_t error =
            selective
                ? xgl_wire_encode_sack_ext_value(value.data(), value.size(),
                                                 number, &bitmap, 1U,
                                                 &value_len)
                : xgl_wire_encode_ack_range_ext_value(value.data(),
                                                      value.size(), number, 0U,
                                                      &range, 1U, &value_len);
        if (error != XGL_OK)
            return XGL_ERR_INVALID_FRAME;
        const auto type = static_cast<uint8_t>(
            selective ? XGL_WIRE_EXT_SACK : XGL_WIRE_EXT_ACK_RANGE);
        if (xgl_wire_encode_ext(ext.data(), ext.size(), type, value.data(),
                                value_len, &ext_len) != XGL_OK)
            return XGL_ERR_INVALID_FRAME;
        xgl_packet_t packet = {};
        packet.source_id = source;
        packet.target_id = 1U;
        packet.connection_id = 11U;
        packet.session_epoch = 22U;
        packet.packet_type = XGL_PACKET_TYPE_ACK;
        packet.reliable = XGL_RELIABILITY_ACK_ONLY;
        packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
        packet.extensions = ext.data();
        packet.extensions_len = ext_len;
        return xgl_transport_receive(&ctx, nullptr, &packet);
    }
};
} /* namespace */

TEST_F(XglTransportAdvancedTest, BusyPeriodExpiresEvenWithZeroTransmitBudget) {
    Init(250U);
    ctx.current_time_ms = UINT32_MAX - 99U;
    ASSERT_EQ(Send(), XGL_OK);
    bool pending = true;
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 149U, 0U, &pending),
              XGL_OK);
    EXPECT_FALSE(ctx.peers->failed);
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 150U, 0U, &pending),
              XGL_ERR_TIMEOUT);
    EXPECT_TRUE(ctx.peers->failed);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 0U);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0], XGL_SCOPE_FAILED);
    EXPECT_EQ(errors[0], XGL_ERR_TIMEOUT);
}

TEST_F(XglTransportAdvancedTest, InvalidAckDoesNotTriggerLifetimeTransition) {
    Init(250U);
    ASSERT_EQ(Send(), XGL_OK);
    ctx.current_time_ms = 250U;
    EXPECT_EQ(Ack(2U, 1U), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(Ack(2U, 1U, true), XGL_ERR_SEQUENCE_ERROR);
    ASSERT_NE(ctx.peers, nullptr);
    EXPECT_FALSE(ctx.peers->failed);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1U);
    EXPECT_TRUE(events.empty());
    EXPECT_EQ(Ack(2U, 0U), XGL_ERR_TIMEOUT);
    EXPECT_TRUE(ctx.peers->failed);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 0U);
    EXPECT_EQ(events, (std::vector<xgl_scope_event_t>{XGL_SCOPE_FAILED}));
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_TIMEOUT}));
}

TEST_F(XglTransportAdvancedTest,
       BudgetRotatesDuePeersWithoutCountingDelayedBusy) {
    Init();
    ASSERT_EQ(Send(2U), XGL_OK);
    ASSERT_EQ(Send(3U), XGL_OK);
    targets.clear();
    bool pending = false;
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 100U, 1U, &pending),
              XGL_OK);
    ASSERT_EQ(targets.size(), 1U);
    EXPECT_TRUE(pending);
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 100U, 1U, &pending),
              XGL_OK);
    ASSERT_EQ(targets.size(), 2U);
    EXPECT_NE(targets[0], targets[1]);
    EXPECT_FALSE(pending);
    submit_result = XGL_ERR_BUSY;
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 300U, 2U, &pending),
              XGL_OK);
    EXPECT_FALSE(pending);
    const auto before = targets.size();
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 301U, 0U, &pending),
              XGL_OK);
    EXPECT_EQ(targets.size(), before);
    EXPECT_FALSE(pending);
}

TEST_F(XglTransportAdvancedTest, DrainedEventOccursOnceAndRestartsLifetime) {
    Init(250U);
    ASSERT_EQ(Send(), XGL_OK);
    ctx.current_time_ms = 200U;
    ASSERT_EQ(Ack(2U, 0U), XGL_OK);
    ASSERT_EQ(Ack(2U, 0U), XGL_OK);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0], XGL_SCOPE_TX_DRAINED);
    ctx.default_timeout_ms = 1000U;
    ASSERT_EQ(Send(), XGL_OK);
    uint32_t timeout = 0U;
    EXPECT_TRUE(xgl_transport_next_timeout(&ctx, 400U, &timeout));
    EXPECT_EQ(timeout, 50U);
    bool pending = false;
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 449U, 0U, &pending),
              XGL_OK);
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 450U, 0U, &pending),
              XGL_ERR_TIMEOUT);
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[1], XGL_SCOPE_FAILED);
}

TEST_F(XglTransportAdvancedTest, ScopedReceivePreservesExactIdentity) {
    Init();
    uint8_t payload = 1U;
    xgl_packet_data_t data = {1U, &payload};
    xgl_packet_t packet = {};
    packet.source_id = 2U;
    packet.target_id = 1U;
    packet.connection_id = 42U;
    packet.session_epoch = 77U;
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.data_type = 3U;
    packet.data = &data;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    ASSERT_EQ(received.size(), 1U);
    EXPECT_EQ(received[0].source_id, 2U);
    EXPECT_EQ(received[0].connection_id, 42U);
    EXPECT_EQ(received[0].session_epoch, 77U);
    EXPECT_EQ(received[0].data_type, 3U);
    EXPECT_TRUE(received[0].reliable);
}

TEST_F(XglTransportAdvancedTest,
       LateAcknowledgementCannotCancelExpiredLifetime) {
    Init(250U);
    ASSERT_EQ(Send(), XGL_OK);
    ctx.current_time_ms = 250U;
    EXPECT_EQ(Ack(2U, 0U), XGL_ERR_TIMEOUT);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0], XGL_SCOPE_FAILED);
    EXPECT_EQ(errors[0], XGL_ERR_TIMEOUT);
}

TEST_F(XglTransportAdvancedTest, CloseRepairsCursorAndReportsCancellationOnce) {
    Init();
    ASSERT_EQ(Send(2U), XGL_OK);
    ASSERT_EQ(Send(3U), XGL_OK);
    bool pending = false;
    ASSERT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 100U, 1U, &pending),
              XGL_OK);
    ASSERT_EQ(xgl_transport_close_scope(&ctx, nullptr, 2U, 11U, 22U), XGL_OK);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_CANCELLED);
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 300U, 1U, &pending),
              XGL_OK);
    EXPECT_FALSE(pending);
    xgl_resource_snapshot_t snapshot = {};
    ASSERT_EQ(xgl_transport_get_resources(&ctx, &snapshot), XGL_OK);
    EXPECT_EQ(snapshot.peer_count, 1U);
    EXPECT_EQ(snapshot.tx_packets, 1U);
    EXPECT_EQ(snapshot.tx_payload_bytes, 1U);
    EXPECT_EQ(snapshot.failed_peers, 0U);
}

TEST_F(XglTransportAdvancedTest, ZeroLifetimePreservesIndefiniteBackpressure) {
    Init();
    ASSERT_EQ(Send(), XGL_OK);
    submit_result = XGL_ERR_BUSY;
    bool pending = false;
    ASSERT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 100000U, 1U, &pending),
              XGL_OK);
    ASSERT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 200000U, 1U, &pending),
              XGL_OK);
    EXPECT_FALSE(ctx.peers->failed);
    EXPECT_TRUE(events.empty());
}

#if XGL_BUILD_WINDOW_CAPACITY >= 2
TEST_F(XglTransportAdvancedTest, LaterAdmissionDoesNotRenewBusyPeriod) {
    Init(250U, 2U);
    ASSERT_EQ(Send(), XGL_OK);
    ctx.current_time_ms = 200U;
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Ack(2U, 1U), XGL_OK);
    ASSERT_TRUE(events.empty());
    bool pending = false;
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 250U, 0U, &pending),
              XGL_ERR_TIMEOUT);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 0U);
}

#endif

#if XGL_FEATURE_OUT_OF_ORDER && XGL_BUILD_WINDOW_CAPACITY >= 2
TEST_F(XglTransportAdvancedTest,
       BufferedDeliveryConsumesAcknowledgementBudget) {
    Init(0U, 2U);
    const uint8_t payload = 0x11U;
    xgl_packet_data_t data = {1U, &payload};
    xgl_packet_t packet = {};
    packet.source_id = 2U;
    packet.target_id = 1U;
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.data = &data;
    packet.packet_number = 1U;
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    accepted_before_busy = 1U;
    packet.packet_number = 0U;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_ERR_BUSY);
    accepted_before_busy = SIZE_MAX;
    ASSERT_EQ(ctx.peers->rx_next_packet_number, 1U);
    received.clear();
    submit_attempts = 0U;
    bool pending = false;
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 1U, 0U, &pending),
              XGL_OK);
    EXPECT_TRUE(pending);
    EXPECT_EQ(submit_attempts, 0U);
    EXPECT_TRUE(received.empty());
    EXPECT_EQ(ctx.peers->rx_buffered_count, 1U);
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 1U, 1U, &pending),
              XGL_OK);
    EXPECT_FALSE(pending);
    EXPECT_EQ(submit_attempts, 1U);
    EXPECT_EQ(received.size(), 1U);
    EXPECT_EQ(ctx.peers->rx_buffered_count, 0U);
}
#endif

TEST_F(XglTransportAdvancedTest,
       ExpiredBusyPeriodRejectsIncomingDataBeforeDelivery) {
    Init(250U);
    ASSERT_EQ(Send(), XGL_OK);
    ctx.current_time_ms = 250U;
    const uint8_t payload = 0x55U;
    xgl_packet_data_t data = {1U, &payload};
    xgl_packet_t packet = {};
    packet.source_id = 2U;
    packet.target_id = 1U;
    packet.connection_id = 11U;
    packet.session_epoch = 22U;
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.data = &data;
    const size_t before = submit_attempts;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_ERR_TIMEOUT);
    EXPECT_TRUE(received.empty());
    EXPECT_EQ(submit_attempts, before);
    EXPECT_TRUE(ctx.peers->failed);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 0U);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_TIMEOUT);
}

#endif
