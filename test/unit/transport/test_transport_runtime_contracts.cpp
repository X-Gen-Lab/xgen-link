/**
 * \file            test_transport_runtime_contracts.cpp
 * \brief           Deferred transport work and observable maintenance failures
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_config.h"
#include "test_host_allocator.h"
#include <api/xgl_instance_internal.h>
#include <transport/xgl_transport_internal.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl_diagnostics.h>

#include <array>
#include <gtest/gtest.h>
#include <vector>

namespace {

class XglTransportRuntimeTest : public ::testing::Test {
  protected:
    xgl_transport_ctx_t context = {};
    xgl_layer_stats_t statistics = {};
    xgl_packet_tx_endpoint_t lower = {};
    uint64_t retries = 0U;
    unsigned attempts = 0U;
    uint16_t failing_target = 0U;
    xgl_error_t failure = XGL_OK;
    std::vector<xgl_error_t> errors;
    xgl_error_t feedback_failure = XGL_OK;
    std::vector<xgl_error_t> feedback_results;
    size_t feedback_index = 0U;
    std::vector<xgl_error_t> acceptance_results;
    size_t acceptance_index = 0U;
    std::vector<uint8_t> delivered;

    static xgl_error_t SendPacket(void* user, xgl_handle_t,
                                  xgl_packet_t* packet) {
        auto* self = static_cast<XglTransportRuntimeTest*>(user);
        if (packet->packet_type == XGL_PACKET_TYPE_ACK) {
            if (self->feedback_index < self->feedback_results.size()) {
                return self->feedback_results[self->feedback_index++];
            }
            return self->feedback_failure;
        }
        if (packet->packet_type != XGL_PACKET_TYPE_DATA) {
            return XGL_OK;
        }
        ++self->attempts;
        return packet->target_id == self->failing_target ? self->failure
                                                         : XGL_OK;
    }

    static xgl_error_t Accept(xgl_handle_t, const xgl_rx_info_t* /*info*/,
                              const uint8_t* data, size_t length, void* user) {
        auto* self = static_cast<XglTransportRuntimeTest*>(user);
        const xgl_error_t result =
            self->acceptance_index < self->acceptance_results.size()
                ? self->acceptance_results[self->acceptance_index++]
                : XGL_OK;
        if (result == XGL_OK) {
            self->delivered.insert(self->delivered.end(), data, data + length);
        }
        return result;
    }

    static void Error(xgl_handle_t, xgl_error_t error, const char*,
                      void* user) {
        static_cast<XglTransportRuntimeTest*>(user)->errors.push_back(error);
    }

    void Init() {
        lower = {this, SendPacket, nullptr, nullptr};
        xgl_transport_config_t config = {};
        config.local_id = 1U;
        config.max_retry_count = 2U;
        config.default_timeout_ms = 100U;
        config.window_size = 4U;
        config.max_frame_size = 64U;
        config.max_peers = 2U;
        config.max_tx_packets = 4U;
        config.max_rx_buffered_packets = 4U;
        config.lower_layer = &lower;
        config.stats = &statistics;
        config.tx_retries = &retries;
        config.allocator = xgl_test_host_allocator();
        config.error_callback = Error;
        config.rx_callback = Accept;
        config.rx_user_data = this;
        config.callback_user_data = this;
        ASSERT_EQ(xgl_transport_init(&context, &config), XGL_OK);
    }

    void TearDown() override {
        xgl_transport_destroy(&context);
    }

    xgl_error_t Send(uint16_t target = 2U) {
        const uint8_t payload = 0U;
        xgl_tx_data_t request = {};
        request.target_id = target;
        request.data = &payload;
        request.data_len = sizeof(payload);
        request.reliable = true;
        return xgl_transport_send(&context, nullptr, &request);
    }

    xgl_error_t Feedback(bool sack, uint32_t number, uint8_t bitmap = 0U) {
        std::array<uint8_t, 32> value = {}, extensions = {};
        size_t value_length = 0U, extensions_length = 0U;
        const xgl_wire_ack_range_t range = {0U, 1U};
        xgl_error_t error = sack ? xgl_wire_encode_sack_ext_value(
                                       value.data(), value.size(), number,
                                       &bitmap, 1U, &value_length)
                                 : xgl_wire_encode_ack_range_ext_value(
                                       value.data(), value.size(), number, 0U,
                                       &range, 1U, &value_length);
        if (error != XGL_OK) {
            return error;
        }
        const auto type = static_cast<uint8_t>(sack ? XGL_WIRE_EXT_SACK
                                                    : XGL_WIRE_EXT_ACK_RANGE);
        error =
            xgl_wire_encode_ext(extensions.data(), extensions.size(), type,
                                value.data(), value_length, &extensions_length);
        if (error != XGL_OK) {
            return error;
        }
        xgl_packet_t packet = {};
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.packet_type = XGL_PACKET_TYPE_ACK;
        packet.reliable = XGL_RELIABILITY_ACK_ONLY;
        packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
        packet.extensions = extensions.data();
        packet.extensions_len = extensions_length;
        return xgl_transport_receive(&context, nullptr, &packet);
    }

    xgl_error_t Receive(uint32_t number, uint16_t source = 2U) {
        const uint8_t payload = static_cast<uint8_t>(number);
        xgl_packet_data_t data = {1U, &payload};
        xgl_packet_t packet = {};
        packet.source_id = source;
        packet.target_id = 1U;
        packet.packet_type = XGL_PACKET_TYPE_DATA;
        packet.packet_number = number;
        packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
        packet.data = &data;
        return xgl_transport_receive(&context, nullptr, &packet);
    }

    uint32_t Deadline(uint32_t now) {
        uint32_t delay = UINT32_MAX;
        EXPECT_TRUE(xgl_transport_next_timeout(&context, now, &delay));
        return delay;
    }
};

#if XGL_BUILD_WINDOW_CAPACITY >= 4
TEST_F(XglTransportRuntimeTest, SackCommitsAckAndDefersTransmissionUntilRun) {
    Init();
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Feedback(true, 0U, 0x02U), XGL_OK);
    EXPECT_EQ(attempts, 2U);
    EXPECT_EQ(xgl_reliable_get_count(&context.peers->reliable_queue), 1U);
    EXPECT_EQ(Deadline(0U), 0U);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 0U), XGL_OK);
    EXPECT_EQ(attempts, 3U);
    const auto* packet =
        xgl_reliable_find_packet_number(&context.peers->reliable_queue, 0U, 2U);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->retry_count, 1U);
    EXPECT_EQ(Deadline(0U), 200U);
}

TEST_F(XglTransportRuntimeTest, DuplicateSackRespectsDeferredBusyRetryDelay) {
    Init();
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Feedback(true, 0U), XGL_OK);
    ASSERT_EQ(Feedback(true, 0U), XGL_OK);
    EXPECT_EQ(attempts, 1U);
    failing_target = 2U;
    failure = XGL_ERR_BUSY;
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 0U), XGL_OK);
    EXPECT_EQ(attempts, 2U);
    EXPECT_EQ(Deadline(0U), 100U);
    ASSERT_EQ(Feedback(true, 0U), XGL_OK);
    EXPECT_EQ(Deadline(20U), 80U);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 99U), XGL_OK);
    EXPECT_EQ(attempts, 2U);
    failure = XGL_OK;
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 100U), XGL_OK);
    EXPECT_EQ(attempts, 3U);
    ASSERT_EQ(Feedback(true, 0U), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 100U), XGL_OK);
    EXPECT_EQ(attempts, 3U);
    EXPECT_EQ(Deadline(100U), 200U);
}

TEST_F(XglTransportRuntimeTest, AckCancelsQueuedFastRetryBeforeMaintenance) {
    Init();
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Feedback(true, 0U), XGL_OK);
    ASSERT_EQ(Feedback(false, 0U), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 0U), XGL_OK);
    EXPECT_EQ(attempts, 1U);
    EXPECT_TRUE(xgl_reliable_is_empty(&context.peers->reliable_queue));
}

TEST_F(XglTransportRuntimeTest, CloseCancelsQueuedFastRetryBeforeMaintenance) {
    Init();
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Feedback(true, 0U), XGL_OK);
    ASSERT_EQ(xgl_transport_close_scope(&context, nullptr, 2U, 0U, 0U), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 0U), XGL_OK);
    EXPECT_EQ(attempts, 1U);
    EXPECT_EQ(context.peers, nullptr);
}

TEST_F(XglTransportRuntimeTest, MaintenanceFailureDoesNotSkipAnotherPeer) {
    Init();
    ASSERT_EQ(Send(3U), XGL_OK);
    ASSERT_EQ(Send(2U), XGL_OK);
    failing_target = 2U;
    failure = XGL_ERR_TX_FAILED;
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 100U), XGL_ERR_TX_FAILED);
    EXPECT_EQ(attempts, 4U);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front(), XGL_ERR_TX_FAILED);
    const auto* live = transport_find_peer_scope(&context, 3U, 0U, 0U);
    ASSERT_NE(live, nullptr);
    EXPECT_FALSE(live->failed);
    const auto* packet =
        xgl_reliable_find_packet_number(&live->reliable_queue, 0U, 3U);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->retry_count, 1U);
}

TEST_F(XglTransportRuntimeTest, RetryLimitIsReturnedOnceAtTerminalTransition) {
    Init();
    context.max_retry_count = 1U;
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 100U), XGL_OK);
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 300U), XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 301U), XGL_OK);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors.front(), XGL_ERR_ACK_TIMEOUT);
}
#endif

#if XGL_FEATURE_OUT_OF_ORDER
TEST_F(XglTransportRuntimeTest, BufferedDeliveryReturnsAckBusyAfterAcceptance) {
    Init();
    ASSERT_EQ(Receive(1U), XGL_OK);
    acceptance_results = {XGL_OK, XGL_ERR_BUSY};
    ASSERT_EQ(Receive(0U), XGL_ERR_BUSY);
    ASSERT_NE(context.peers, nullptr);
    ASSERT_EQ(context.peers->rx_buffered_count, 1U);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0U}));

    feedback_failure = XGL_ERR_BUSY;
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 1U), XGL_ERR_BUSY);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0U, 1U}));
    EXPECT_EQ(context.peers->rx_buffered_count, 0U);
    EXPECT_EQ(context.peers->rx_next_packet_number, 2U);
    EXPECT_FALSE(context.peers->failed);
    EXPECT_TRUE(errors.empty());

    feedback_failure = XGL_OK;
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 2U), XGL_OK);
    EXPECT_EQ(Receive(1U), XGL_OK);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0U, 1U}));
}

TEST_F(XglTransportRuntimeTest,
       BufferedHardFailureOutranksEarlierTemporaryAckFailure) {
    Init();
    ASSERT_EQ(Receive(1U), XGL_OK);
    ASSERT_EQ(Receive(2U), XGL_OK);
    acceptance_results = {XGL_OK, XGL_ERR_BUSY};
    ASSERT_EQ(Receive(0U), XGL_ERR_BUSY);
    ASSERT_NE(context.peers, nullptr);
    ASSERT_EQ(context.peers->rx_buffered_count, 2U);

    acceptance_results = {XGL_OK, XGL_ERR_INVALID_PARAM};
    acceptance_index = 0U;
    feedback_failure = XGL_ERR_BUSY;
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 1U), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0U, 1U}));
    EXPECT_TRUE(context.peers->failed);
    EXPECT_EQ(context.peers->rx_buffered_count, 0U);
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_INVALID_PARAM}));
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 2U), XGL_OK);
    EXPECT_EQ(errors.size(), 1U);
}

TEST_F(XglTransportRuntimeTest,
       BufferedHardFailureOutranksOtherPeersTemporaryAckFailure) {
    Init();
    ASSERT_EQ(Receive(1U, 3U), XGL_OK);
    acceptance_results = {XGL_OK, XGL_ERR_BUSY};
    ASSERT_EQ(Receive(0U, 3U), XGL_ERR_BUSY);
    ASSERT_EQ(Receive(1U), XGL_OK);
    acceptance_index = 0U;
    ASSERT_EQ(Receive(0U), XGL_ERR_BUSY);
    auto* accepting_peer = transport_find_peer_scope(&context, 2U, 0U, 0U);
    auto* failing_peer = transport_find_peer_scope(&context, 3U, 0U, 0U);
    ASSERT_NE(accepting_peer, nullptr);
    ASSERT_NE(failing_peer, nullptr);
    ASSERT_EQ(accepting_peer->rx_buffered_count, 1U);
    ASSERT_EQ(failing_peer->rx_buffered_count, 1U);

    acceptance_results = {XGL_OK, XGL_ERR_INVALID_PARAM};
    acceptance_index = 0U;
    feedback_failure = XGL_ERR_BUSY;
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 1U), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0U, 0U, 1U}));
    EXPECT_FALSE(accepting_peer->failed);
    EXPECT_EQ(accepting_peer->rx_buffered_count, 0U);
    EXPECT_TRUE(failing_peer->failed);
    EXPECT_EQ(failing_peer->rx_buffered_count, 0U);
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_INVALID_PARAM}));
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 2U), XGL_OK);
    EXPECT_EQ(errors.size(), 1U);
}

TEST_F(XglTransportRuntimeTest,
       BufferedHardAckFailureOutranksEarlierTemporaryAckFailure) {
    Init();
    ASSERT_EQ(Receive(1U), XGL_OK);
    ASSERT_EQ(Receive(2U), XGL_OK);
    acceptance_results = {XGL_OK, XGL_ERR_BUSY};
    ASSERT_EQ(Receive(0U), XGL_ERR_BUSY);
    ASSERT_NE(context.peers, nullptr);
    ASSERT_EQ(context.peers->rx_buffered_count, 2U);

    feedback_results = {XGL_ERR_BUSY, XGL_ERR_TX_FAILED};
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 1U), XGL_ERR_TX_FAILED);
    EXPECT_EQ(feedback_index, 2U);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0U, 1U, 2U}));
    EXPECT_EQ(context.peers->rx_buffered_count, 0U);
    EXPECT_EQ(context.peers->rx_next_packet_number, 3U);
    EXPECT_FALSE(context.peers->failed);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(xgl_transport_run(&context, nullptr, 2U), XGL_OK);
    EXPECT_EQ(Receive(1U), XGL_OK);
    EXPECT_EQ(delivered, (std::vector<uint8_t>{0U, 1U, 2U}));
}
#endif

struct PublicRuntime {
    xgl_error_t tx_result = XGL_OK;
    std::vector<xgl_error_t> errors;

    static xgl_error_t Tx(const uint8_t*, size_t, void* user) {
        return static_cast<PublicRuntime*>(user)->tx_result;
    }

    static xgl_error_t Rx(uint8_t*, size_t* length, void*) {
        *length = 0U;
        return XGL_OK;
    }

    static void Error(xgl_handle_t, xgl_error_t error, const char*,
                      void* user) {
        static_cast<PublicRuntime*>(user)->errors.push_back(error);
    }
};

TEST(XglTransportPublicRuntimeTest, StepReturnsHardRetryAndExhaustionErrors) {
    for (bool hard_failure : {false, true}) {
        SCOPED_TRACE(hard_failure);
        PublicRuntime state;
        xgl_phy_ops_t phy = {};
        phy.tx = PublicRuntime::Tx;
        phy.rx = PublicRuntime::Rx;
        phy.user_data = &state;
        xgl_route_item_t route = {};
        route.target_id = 2U;
        route.phy = &phy;
        route.max_frame_size = 128U;
        route.read_freq_hz = 1000U;
        xgl_config_t config;
        config = xgl_test_single_packet_config();
        config.route_table = &route;
        config.route_table_len = 1U;
        config.protocol.max_retry_count = 1U;
        config.error_callback = PublicRuntime::Error;
        config.rx_user_data = &state;
        config.callback_user_data = &state;
        xgl_handle_t handle = xgl_test_init_allocated(&config);
        ASSERT_NE(handle, nullptr);
        const uint8_t payload = 42U;
        xgl_tx_data_t data = {};
        data.target_id = 2U;
        data.data = &payload;
        data.data_len = 1U;
        data.reliable = true;
        data.timeout_ms = 100U;
        ASSERT_EQ(xgl_send_at(handle, &data, 0U), XGL_OK);
        const xgl_work_budget_t budget = {0U, 1000U};
        if (hard_failure) {
            state.tx_result = XGL_ERR_TX_FAILED;
            EXPECT_EQ(xgl_step(handle, 100U, &budget), XGL_ERR_TX_FAILED);
        } else {
            ASSERT_EQ(xgl_step(handle, 100U, &budget), XGL_OK);
            EXPECT_EQ(xgl_step(handle, 300U, &budget), XGL_ERR_ACK_TIMEOUT);
        }
        EXPECT_FALSE(state.errors.empty());
        xgl_destroy(handle);
    }
}

} /* namespace */
