/**
 * \file            test_transport_advanced.cpp
 * \brief           Optional scoped runtime, deadlines and bounded maintenance
 */

#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>
#include <xgen/memory/libc_allocator.h>

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

    void Init(uint32_t lifetime = 0U, uint32_t reassembly = 0U,
              uint8_t window = 1U) {
        lower = {this, Submit};
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
        config.allocator = xgm_allocator_libc();
        config.tx_lifetime_ms = lifetime;
        config.reassembly_timeout_ms = reassembly;
        config.rx_scoped_callback = Accept;
        config.scope_event_callback = Event;
        config.extended_user_data = this;
#if XGL_FEATURE_FRAGMENTATION
        config.enable_fragmentation = true;
        config.max_message_size = 128U;
        config.max_tx_message_bytes = 384U;
        config.max_reassembly_bytes = 384U;
        config.max_reassembly_slots = 3U;
#endif
        ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);
        initialized = true;
    }

    void TearDown() override {
        if (initialized)
            xgl_transport_destroy(&ctx);
    }

    xgl_error_t Send(uint16_t target = 2U, size_t length = 1U) {
        const std::array<uint8_t, 128> payload = {};
        xgl_tx_data_t data = {};
        data.target_id = target;
        data.connection_id = 11U;
        data.session_epoch = 22U;
        data.reliable = true;
        data.data = payload.data();
        data.data_len = length;
        return xgl_transport_send(&ctx, nullptr, &data);
    }
#if XGL_FEATURE_FRAGMENTATION
    xgl_error_t Fragment(uint32_t packet_number, uint32_t offset,
                         uint32_t length = 4U, bool reliable = true) {
        const uint8_t payload[] = {0x11U, 0x22U};
        std::array<uint8_t, 32> value = {}, extension = {};
        size_t value_len = 0U, extension_len = 0U;
        if (xgl_wire_encode_fragment_ext_value(value.data(), value.size(), 7U,
                                               offset, length,
                                               &value_len) != XGL_OK)
            return XGL_ERR_INVALID_FRAME;
        if (xgl_wire_encode_ext(extension.data(), extension.size(),
                                XGL_WIRE_EXT_FRAGMENT, value.data(), value_len,
                                &extension_len) != XGL_OK)
            return XGL_ERR_INVALID_FRAME;
        xgl_packet_data_t data = {sizeof(payload), payload};
        xgl_packet_t packet = {};
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.connection_id = 42U;
        packet.session_epoch = 77U;
        packet.packet_type = XGL_PACKET_TYPE_DATA;
        packet.data_type = 3U;
        packet.reliable =
            reliable ? XGL_RELIABILITY_ACK_ELICITING : XGL_RELIABILITY_NONE;
        packet.packet_number = packet_number;
        packet.fragment = true;
        packet.flags = XGL_WIRE_FLAG_FRAGMENTED | XGL_WIRE_FLAG_HAS_EXTENSIONS;
        packet.extensions = extension.data();
        packet.extensions_len = extension_len;
        packet.data = &data;
        return xgl_transport_receive(&ctx, nullptr, &packet);
    }
#endif
    xgl_error_t Ack(uint16_t source, uint32_t number) {
        std::array<uint8_t, 32> value = {}, ext = {};
        size_t value_len = 0U, ext_len = 0U;
        const xgl_wire_ack_range_t range = {0U, 1U};
        if (xgl_wire_encode_ack_range_ext_value(value.data(), value.size(),
                                                number, 0U, &range, 1U,
                                                &value_len) != XGL_OK)
            return XGL_ERR_INVALID_FRAME;
        if (xgl_wire_encode_ext(ext.data(), ext.size(), XGL_WIRE_EXT_ACK_RANGE,
                                value.data(), value_len, &ext_len) != XGL_OK)
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
}  // namespace

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

#if XGL_FEATURE_FRAGMENTATION
TEST_F(XglTransportAdvancedTest,
       AdmittedMessageExpiresDuringLocalBackpressure) {
    Init(250U);
    submit_result = XGL_ERR_BUSY;
    ASSERT_EQ(Send(2U, 128U), XGL_OK);
    EXPECT_EQ(ctx.tx_message_bytes, 128U);
    bool pending = false;
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 250U, 0U, &pending),
              XGL_ERR_TIMEOUT);
    EXPECT_EQ(ctx.tx_message_bytes, 0U);
    EXPECT_TRUE(ctx.peers->failed);
}

TEST_F(XglTransportAdvancedTest, ReassemblyUsesConfiguredAbsoluteDeadline) {
    Init(0U, 12000U);
    ASSERT_EQ(Fragment(0U, 0U, 6U), XGL_OK);
    ctx.current_time_ms = 11000U;
    ASSERT_EQ(Fragment(1U, 2U, 6U), XGL_OK);
    uint32_t timeout = 0U;
    EXPECT_TRUE(xgl_transport_next_timeout(&ctx, 11999U, &timeout));
    EXPECT_EQ(timeout, 1U);
    bool pending = false;
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 11999U, 0U, &pending),
              XGL_OK);
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 12000U, 0U, &pending),
              XGL_ERR_TIMEOUT);
    EXPECT_EQ(ctx.fragment_mgr->current_reassembly_bytes, 0U);
    EXPECT_TRUE(ctx.peers->failed);
    EXPECT_TRUE(received.empty());
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0], XGL_SCOPE_FAILED);
}


TEST_F(XglTransportAdvancedTest, LateReliableFinalFragmentCannotCompleteExpiredMessage) {
    Init(0U, 250U);
    ASSERT_EQ(Fragment(0U, 0U), XGL_OK);
    ctx.current_time_ms = 250U;
    EXPECT_EQ(Fragment(1U, 2U), XGL_ERR_TIMEOUT);
    EXPECT_TRUE(received.empty());
    EXPECT_TRUE(ctx.peers->failed);
    EXPECT_EQ(ctx.fragment_mgr->current_reassembly_bytes, 0U);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0], XGL_SCOPE_FAILED);
    EXPECT_EQ(errors[0], XGL_ERR_TIMEOUT);
}

TEST_F(XglTransportAdvancedTest, LateUnreliableFinalFragmentExpiresOnlyItsMessage) {
    Init(0U, 250U);
    ASSERT_EQ(Fragment(0U, 0U, 4U, false), XGL_OK);
    ctx.current_time_ms = 250U;
    EXPECT_EQ(Fragment(1U, 2U, 4U, false), XGL_ERR_TIMEOUT);
    EXPECT_TRUE(received.empty());
    EXPECT_FALSE(ctx.peers->failed);
    EXPECT_EQ(ctx.fragment_mgr->current_reassembly_bytes, 0U);
    EXPECT_TRUE(events.empty());
    ASSERT_EQ(Fragment(0U, 0U, 4U, false), XGL_OK);
    ASSERT_EQ(Fragment(1U, 2U, 4U, false), XGL_OK);
    ASSERT_EQ(received.size(), 1U);
    EXPECT_FALSE(received[0].reliable);
}

TEST_F(XglTransportAdvancedTest,
       PendingCompleteMessagePreservesScopeAndSnapshot) {
    Init();
    accept_result = XGL_ERR_BUSY;
    ASSERT_EQ(Fragment(0U, 0U), XGL_OK);
    ASSERT_EQ(Fragment(1U, 2U), XGL_OK);
    xgl_resource_snapshot_t first = {}, second = {};
    ASSERT_EQ(xgl_transport_get_resources(&ctx, &first), XGL_OK);
    ASSERT_EQ(xgl_transport_get_resources(&ctx, &second), XGL_OK);
    EXPECT_EQ(first.pending_rx_messages, 1U);
    EXPECT_EQ(first.reassembly_slots, 0U);
    EXPECT_EQ(first.reassembly_bytes, 4U);
    EXPECT_EQ(second.reassembly_bytes, first.reassembly_bytes);
    accept_result = XGL_OK;
    bool pending = false;
    ASSERT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 1U, 0U, &pending),
              XGL_OK);
    ASSERT_EQ(received.size(), 2U);
    EXPECT_EQ(received[1].source_id, 2U);
    EXPECT_EQ(received[1].connection_id, 42U);
    EXPECT_EQ(received[1].session_epoch, 77U);
    EXPECT_EQ(received[1].data_type, 3U);
    EXPECT_TRUE(received[1].reliable);
    ASSERT_EQ(xgl_transport_get_resources(&ctx, &second), XGL_OK);
    EXPECT_EQ(second.reassembly_bytes, 0U);
    EXPECT_EQ(second.pending_rx_messages, 0U);
}

TEST_F(XglTransportAdvancedTest,
       LifetimeSurvivesMessageTailAndAcknowledgement) {
    Init(250U);
    ASSERT_EQ(Send(2U, 40U), XGL_OK);
    bool pending = false;
    uint32_t packet = 0U;
    while (ctx.peers->tx_message.data != nullptr) {
        ASSERT_LT(packet, 10U);
        ASSERT_EQ(Ack(2U, packet++), XGL_OK);
        ASSERT_TRUE(events.empty());
        ASSERT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 100U, 1U, &pending),
                  XGL_OK);
    }
    EXPECT_EQ(ctx.tx_message_bytes, 0U);
    EXPECT_GT(xgl_reliable_get_count(&ctx.peers->reliable_queue), 0U);
    EXPECT_EQ(xgl_transport_run_bounded(&ctx, nullptr, 250U, 0U, &pending),
              XGL_ERR_TIMEOUT);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0], XGL_SCOPE_FAILED);
}
#endif

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
    Init(250U, 0U, 2U);
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
    Init(0U, 0U, 2U);
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
#endif
