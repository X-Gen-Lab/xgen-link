/**
 * \file            test_transport.cpp
 * \brief           Transport layer unit tests
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_host_allocator.h"

#include <network/xgl_route.h>
#include <transport/xgl_reliable.h>
#include <transport/xgl_transport.h>
#include <transport/xgl_window.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl.h>

#include "transport/xgl_transport_internal.h"
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>
#include <vector>

template <typename T, typename = void>
struct HasLegacyAckHandler : std::false_type {};

template <typename T>
struct HasLegacyAckHandler<T,
                           std::void_t<decltype(std::declval<T>().ack_handler)>>
    : std::true_type {};

static_assert(
    !HasLegacyAckHandler<xgl_transport_ctx_t>::value,
    "xgl_transport_ctx_t must not retain legacy 8-bit ACK handler state");

namespace {

struct LowerLayerSpy {
    int send_count = 0;
    unsigned busy_data_attempts = 0;
    xgl_error_t data_error = XGL_OK;
    uint32_t selected_error_packet = UINT32_MAX;
    xgl_error_t selected_error = XGL_OK;
    xgl_packet_t last_packet = {};
    std::vector<xgl_packet_t> sent_packets;
    std::vector<std::vector<uint8_t>> sent_payloads;
    std::vector<std::vector<uint8_t>> sent_extensions;
};

struct RxTracker {
    int receive_count = 0;
    std::vector<std::vector<uint8_t>> payloads;
};

constexpr uint8_t XGL_TEST_CONTROL_HELLO = 0x0E;
constexpr uint8_t XGL_TEST_CONTROL_RESET = 0x0F;

static xgl_error_t spy_send(void* ctx, xgl_handle_t handle,
                            xgl_packet_t* packet) {
    (void)handle;

    auto* spy = static_cast<LowerLayerSpy*>(ctx);
    if (spy == nullptr || packet == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }

    if (spy->busy_data_attempts > 0 &&
        packet->packet_type == XGL_PACKET_TYPE_DATA) {
        spy->busy_data_attempts--;
        return XGL_ERR_BUSY;
    }
    if (packet->packet_type == XGL_PACKET_TYPE_DATA &&
        packet->packet_number == spy->selected_error_packet) {
        return spy->selected_error;
    }
    if (packet->packet_type == XGL_PACKET_TYPE_DATA &&
        spy->data_error != XGL_OK) {
        return spy->data_error;
    }
    spy->send_count++;
    spy->last_packet = *packet;
    spy->sent_packets.push_back(*packet);
    if (packet->data != nullptr && packet->data->data != nullptr &&
        packet->data->data_len > 0U) {
        spy->sent_payloads.emplace_back(
            packet->data->data, packet->data->data + packet->data->data_len);
    } else {
        spy->sent_payloads.emplace_back();
    }
    if (packet->extensions != nullptr && packet->extensions_len > 0U) {
        spy->sent_extensions.emplace_back(
            packet->extensions, packet->extensions + packet->extensions_len);
    } else {
        spy->sent_extensions.emplace_back();
    }
    return XGL_OK;
}

static void* always_fail_malloc(void*, size_t size) {
    (void)size;
    return nullptr;
}

static void always_fail_free(void*, void* ptr) {
    std::free(ptr);
}

static void spy_receive(xgl_handle_t handle, uint16_t source_id,
                        uint8_t data_type, const uint8_t* data, size_t len,
                        void* user_data) {
    (void)handle;
    (void)source_id;
    (void)data_type;
    auto* tracker = static_cast<RxTracker*>(user_data);
    if (tracker != nullptr) {
        tracker->receive_count++;
        if (data != nullptr && len > 0U) {
            tracker->payloads.emplace_back(data, data + len);
        } else {
            tracker->payloads.emplace_back();
        }
    }
}

static xgl_transport_config_t
make_transport_config(xgl_packet_tx_endpoint_t* lower_layer,
                      xgl_layer_stats_t* stats, uint64_t* tx_retries) {
    xgl_transport_config_t config = {};
    config.allocator = xgl_test_host_allocator();
    config.max_peers = 32;
    config.max_tx_packets = 4096;
    config.max_rx_buffered_packets = 4096;
    config.local_id = 1;
    config.max_retry_count = 3;
    config.default_timeout_ms = 100;
    config.window_size = 1;
    config.max_frame_size = 128;
    config.lower_layer = lower_layer;
    config.stats = stats;
    config.tx_retries = tx_retries;
    return config;
}

static xgl_transport_peer_state_t* find_peer(xgl_transport_ctx_t* ctx,
                                             uint16_t peer_id) {
    for (xgl_transport_peer_state_t* peer = ctx->peers; peer != nullptr;
         peer = peer->next) {
        if (peer->peer_id == peer_id) {
            return peer;
        }
    }
    return nullptr;
}

static xgl_transport_peer_state_t*
find_peer_scope_for_test(xgl_transport_ctx_t* ctx, uint16_t peer_id,
                         uint32_t connection_id, uint32_t session_epoch) {
    for (xgl_transport_peer_state_t* peer = ctx->peers; peer != nullptr;
         peer = peer->next) {
        if (peer->peer_id == peer_id && peer->connection_id == connection_id &&
            peer->session_epoch == session_epoch) {
            return peer;
        }
    }
    return nullptr;
}

static void expect_sack_packet_for_base(const LowerLayerSpy& spy,
                                        uint32_t base_packet) {
    ASSERT_FALSE(spy.sent_packets.empty());
    const xgl_packet_t& packet = spy.last_packet;
    EXPECT_EQ(packet.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(packet.reliable, XGL_RELIABILITY_ACK_ONLY);
    ASSERT_FALSE(spy.sent_extensions.empty());
    ASSERT_FALSE(spy.sent_extensions.back().empty());

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor,
                                       spy.sent_extensions.back().data(),
                                       spy.sent_extensions.back().size()),
              XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    ASSERT_EQ(ext.type, XGL_WIRE_EXT_SACK);

    xgl_wire_sack_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_sack_view(ext.value, ext.len, &view), XGL_OK);
    EXPECT_EQ(view.base_packet, base_packet);
    ASSERT_GT(view.bitmap_len, 0U);
}

static uint32_t s_regression_time_ms;

class TransportRegressionTest : public ::testing::Test {
  protected:
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower = {};
    xgl_layer_stats_t stats = {};
    uint64_t retries = 0;
    xgl_transport_ctx_t ctx = {};
    bool initialized = false;
    bool accept_blocked = false;
    uint8_t blocked_byte = 'b';
    std::vector<std::vector<uint8_t>> accepted_payloads;
    std::vector<xgl_error_t> errors;

    static void Error(xgl_handle_t, xgl_error_t error, const char*,
                      void* user_data) {
        static_cast<TransportRegressionTest*>(user_data)->errors.push_back(
            error);
    }

    static xgl_error_t Accept(xgl_handle_t, uint16_t, uint8_t,
                              const uint8_t* data, size_t len,
                              void* user_data) {
        auto* test = static_cast<TransportRegressionTest*>(user_data);
        if (test->accept_blocked && len > 0 && data[0] == test->blocked_byte) {
            return XGL_ERR_BUSY;
        }
        test->accepted_payloads.emplace_back(data, data + len);
        return XGL_OK;
    }

    xgl_error_t Receive(uint32_t number, uint8_t byte) {
        xgl_packet_data_t payload = {1U, &byte};
        xgl_packet_t packet = {};
        packet.source_id = 2;
        packet.target_id = 1;
        packet.packet_type = XGL_PACKET_TYPE_DATA;
        packet.packet_number = number;
        packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
        packet.data = &payload;
        return xgl_transport_receive(&ctx, nullptr, &packet);
    }

    void SetUp() override {
        s_regression_time_ms = 100;
        xgl_packet_tx_endpoint_init(&lower, &spy, spy_send);
    }

    void TearDown() override {
        if (initialized) {
            xgl_transport_destroy(&ctx);
        }
    }

    void Init(uint8_t window = 4) {
        auto config = make_transport_config(&lower, &stats, &retries);
        config.window_size = window;
        config.max_retry_count = 1;
        config.rx_accept_callback = Accept;
        config.error_callback = Error;
        config.callback_user_data = this;
        ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);
        ctx.current_time_ms = s_regression_time_ms;
        initialized = true;
    }

    xgl_error_t Send(uint32_t connection = 0, uint32_t epoch = 0) {
        static const uint8_t payload[] = {'t'};
        xgl_tx_data_t tx = {};
        tx.target_id = 2;
        tx.data = payload;
        tx.data_len = sizeof(payload);
        tx.reliable = true;
        tx.timeout_ms = 100;
        tx.connection_id = connection;
        tx.session_epoch = epoch;
        return xgl_transport_send(&ctx, nullptr, &tx);
    }

    xgl_error_t Ack(uint32_t largest, uint16_t length, uint32_t connection = 0,
                    uint32_t epoch = 0, bool trailing_invalid = false) {
        uint8_t value[32] = {}, extensions[40] = {};
        size_t value_len = 0, extensions_len = 0;
        xgl_wire_ack_range_t range = {};
        range.gap = 0;
        range.length = length;
        if (xgl_wire_encode_ack_range_ext_value(value, sizeof(value), largest,
                                                0, &range, 1,
                                                &value_len) != XGL_OK) {
            return XGL_ERR_INVALID_FRAME;
        }
        if (xgl_wire_encode_ext(extensions, sizeof(extensions),
                                XGL_WIRE_EXT_ACK_RANGE, value, value_len,
                                &extensions_len) != XGL_OK) {
            return XGL_ERR_INVALID_FRAME;
        }
        if (trailing_invalid) {
            extensions[extensions_len++] = XGL_WIRE_EXT_SESSION;
        }
        xgl_packet_t packet = {};
        packet.source_id = 2;
        packet.target_id = 1;
        packet.packet_type = XGL_PACKET_TYPE_ACK;
        packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
        packet.reliable = XGL_RELIABILITY_ACK_ONLY;
        packet.connection_id = connection;
        packet.session_epoch = epoch;
        packet.extensions = extensions;
        packet.extensions_len = extensions_len;
        return xgl_transport_receive(&ctx, nullptr, &packet);
    }

    xgl_error_t Sack(uint8_t bitmap) {
        uint8_t value[16] = {}, extensions[20] = {};
        size_t value_len = 0, extensions_len = 0;
        if (xgl_wire_encode_sack_ext_value(value, sizeof(value), 0, &bitmap, 1,
                                           &value_len) != XGL_OK ||
            xgl_wire_encode_ext(extensions, sizeof(extensions),
                                XGL_WIRE_EXT_SACK, value, value_len,
                                &extensions_len) != XGL_OK) {
            return XGL_ERR_INVALID_FRAME;
        }
        xgl_packet_t packet = {};
        packet.source_id = 2;
        packet.target_id = 1;
        packet.packet_type = XGL_PACKET_TYPE_ACK;
        packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
        packet.reliable = XGL_RELIABILITY_ACK_ONLY;
        packet.extensions = extensions;
        packet.extensions_len = extensions_len;
        return xgl_transport_receive(&ctx, nullptr, &packet);
    }
};

} /* namespace */

TEST_F(TransportRegressionTest, RetryExhaustionRequiresNewEpochAfterReset) {
    Init(1);
    ASSERT_EQ(Send(), XGL_OK);
    s_regression_time_ms = 201;
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, s_regression_time_ms), XGL_OK);
    s_regression_time_ms = 402;
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, s_regression_time_ms),
              XGL_ERR_ACK_TIMEOUT);
    ASSERT_NE(ctx.peers, nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 0U);
    EXPECT_EQ(Send(), XGL_ERR_ACK_TIMEOUT);
    const auto previous_tx_errors = stats.tx_errors;
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 1000), XGL_OK);
    EXPECT_EQ(stats.tx_errors, previous_tx_errors);
    xgl_packet_t reset = {};
    reset.source_id = 2;
    reset.packet_type = XGL_PACKET_TYPE_CONTROL;
    reset.data_type = XGL_TEST_CONTROL_RESET;
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &reset), XGL_OK);
    EXPECT_EQ(Send(), XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(Send(0, 1), XGL_OK);
}

TEST_F(TransportRegressionTest, AckForUnknownScopeCannotReleaseKnownScope) {
    Init(1);
    ASSERT_EQ(Send(11, 100), XGL_OK);
    ASSERT_NE(ctx.peers, nullptr);
    EXPECT_EQ(Ack(0, 1, 999, 200), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1U);
    EXPECT_EQ(ctx.peers->tx_window.send_base_packet_number, 0U);
    EXPECT_EQ(Send(11, 100), XGL_ERR_WINDOW_FULL);
}

TEST_F(TransportRegressionTest, FutureAckIsRejectedWithoutPartialMutation) {
    Init();
    ASSERT_EQ(Send(), XGL_OK);
    EXPECT_EQ(Ack(3, 4), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1U);
    EXPECT_EQ(ctx.peers->tx_window.send_base_packet_number, 0U);
    EXPECT_EQ(ctx.peers->tx_window.next_packet_number, 1U);
    EXPECT_EQ(Ack(0, 1), XGL_OK);
    EXPECT_EQ(Send(), XGL_OK);
}

TEST_F(TransportRegressionTest, MalformedTrailingExtensionCannotPartiallyAck) {
    Init();
    ASSERT_EQ(Send(), XGL_OK);
    EXPECT_EQ(Ack(0, 1, 0, 0, true), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1U);
    EXPECT_EQ(ctx.peers->tx_window.send_base_packet_number, 0U);
}

TEST_F(TransportRegressionTest, DuplicateAckIsIdempotent) {
    Init();
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Ack(0, 1), XGL_OK);
    EXPECT_EQ(Ack(0, 1), XGL_OK);
    EXPECT_EQ(ctx.peers->tx_window.send_base_packet_number, 1U);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 0U);
}

TEST_F(TransportRegressionTest, SackCommitsAllAcknowledgementsBeforeBusyRetry) {
    Init();
    ctx.max_tx_packets = 3;
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Send(), XGL_OK);

    const uint8_t bitmap[] = {0x06};
    uint8_t value[16] = {}, extensions[20] = {};
    size_t value_len = 0, extensions_len = 0;
    ASSERT_EQ(xgl_wire_encode_sack_ext_value(value, sizeof(value), 0, bitmap,
                                             sizeof(bitmap), &value_len),
              XGL_OK);
    ASSERT_EQ(xgl_wire_encode_ext(extensions, sizeof(extensions),
                                  XGL_WIRE_EXT_SACK, value, value_len,
                                  &extensions_len),
              XGL_OK);
    xgl_packet_t packet = {};
    packet.source_id = 2;
    packet.target_id = 1;
    packet.packet_type = XGL_PACKET_TYPE_ACK;
    packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
    packet.reliable = XGL_RELIABILITY_ACK_ONLY;
    packet.extensions = extensions;
    packet.extensions_len = extensions_len;

    spy.busy_data_attempts = 1;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, s_regression_time_ms), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1U);
    EXPECT_EQ(ctx.peers->tx_window.send_base_packet_number, 0U);
    EXPECT_EQ(retries, 0U);
    EXPECT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Ack(0, 1), XGL_OK);
    EXPECT_EQ(ctx.peers->tx_window.send_base_packet_number, 3U);
}

TEST_F(TransportRegressionTest, InvalidSendPlanDoesNotConsumePeerOrEmitHello) {
    Init();
    ctx.max_peers = 1;
    const uint8_t payload[256] = {};
    xgl_tx_data_t tx = {};
    tx.target_id = 2;
    tx.connection_id = 77;
    tx.data = payload;
    tx.data_len = sizeof(payload);
    tx.reliable = true;

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(ctx.peers, nullptr);
    EXPECT_EQ(spy.send_count, 0);
    EXPECT_EQ(Send(), XGL_OK);
}

TEST_F(TransportRegressionTest, BusyInOrderPacketIsNotAcknowledged) {
    Init();
    accept_blocked = true;
    EXPECT_EQ(Receive(0, 'b'), XGL_ERR_BUSY);
    EXPECT_EQ(ctx.peers, nullptr);
    EXPECT_EQ(spy.send_count, 0);
    EXPECT_TRUE(accepted_payloads.empty());
    accept_blocked = false;
    EXPECT_EQ(Receive(0, 'b'), XGL_OK);
    EXPECT_EQ(spy.send_count, 1);
    EXPECT_EQ(ctx.peers->rx_next_packet_number, 1U);
    EXPECT_EQ(accepted_payloads.size(), 1U);
    EXPECT_EQ(Receive(0, 'b'), XGL_OK);
    EXPECT_EQ(accepted_payloads.size(), 1U);
}

TEST_F(TransportRegressionTest,
       SackOwnedPacketSurvivesApplicationBackpressure) {
    Init();
    accept_blocked = true;
    ASSERT_EQ(Receive(1, 'b'), XGL_OK);
    ASSERT_EQ(ctx.peers->rx_buffered_count, 1U);
    ASSERT_EQ(Receive(0, 'a'), XGL_ERR_BUSY);
    EXPECT_EQ(ctx.peers->rx_next_packet_number, 1U);
    EXPECT_EQ(ctx.peers->rx_buffered_count, 1U);
    EXPECT_EQ(accepted_payloads.size(), 1U);
    uint32_t timeout = 99;
    EXPECT_FALSE(xgl_transport_next_timeout(&ctx, 100, &timeout));
    accept_blocked = false;
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 101), XGL_OK);
    EXPECT_EQ(ctx.peers->rx_next_packet_number, 2U);
    EXPECT_EQ(ctx.peers->rx_buffered_count, 0U);
    ASSERT_EQ(accepted_payloads.size(), 2U);
    EXPECT_EQ(accepted_payloads[1], std::vector<uint8_t>({'b'}));
}

TEST_F(TransportRegressionTest, ExplicitTimeZeroSchedulesRetransmission) {
    Init(1);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 0), XGL_OK);
    ASSERT_EQ(Send(), XGL_OK);
    uint32_t timeout = 0;
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 0, &timeout));
    EXPECT_EQ(timeout, 100U);
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 100, &timeout));
    EXPECT_EQ(timeout, 0U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 100), XGL_OK);
    EXPECT_EQ(retries, 1U);
}

TEST_F(TransportRegressionTest,
       IdlePeerRetainsUsedTransmitNumbersUntilExplicitClose) {
    Init();
    ctx.max_peers = 1;
    ctx.peer_idle_timeout_ms = 1000;
    ASSERT_EQ(Send(), XGL_OK);
    EXPECT_FALSE(xgl_transport_can_send_to(&ctx, 3, 0, 0));
    EXPECT_EQ(Send(1, 1), XGL_ERR_NO_MEMORY);
    ASSERT_EQ(Ack(0, 1), XGL_OK);
    uint32_t timeout = 0;
    EXPECT_FALSE(xgl_transport_next_timeout(&ctx, 100, &timeout));
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 1100), XGL_OK);
    ASSERT_NE(ctx.peers, nullptr);
    EXPECT_FALSE(xgl_transport_can_send_to(&ctx, 3, 0, 0));
    ASSERT_EQ(Send(), XGL_OK);
    EXPECT_EQ(spy.last_packet.packet_number, 1U);
    ASSERT_EQ(Ack(0, 1), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1U);
    ASSERT_EQ(xgl_transport_close_scope(&ctx, nullptr, 2, 0, 0), XGL_OK);
    EXPECT_TRUE(xgl_transport_can_send_to(&ctx, 3, 0, 1));
}

TEST_F(TransportRegressionTest, TimeoutCannotOverflowSignedReliableDuration) {
    Init();
    const uint8_t payload = 'a';
    xgl_tx_data_t tx = {};
    tx.target_id = 2;
    tx.reliable = true;
    tx.data = &payload;
    tx.data_len = 1;
    tx.timeout_ms = static_cast<uint32_t>(INT32_MAX) + 1U;
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(spy.send_count, 0);
    EXPECT_EQ(ctx.peers, nullptr);
}

TEST_F(TransportRegressionTest,
       RetransmissionHardFailureReleasesAcceptedPacket) {
    Init(1);
    ASSERT_EQ(Send(), XGL_OK);
    spy.data_error = XGL_ERR_TX_FAILED;
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 200), XGL_ERR_TX_FAILED);
    EXPECT_TRUE(ctx.peers->failed);
    EXPECT_TRUE(xgl_reliable_is_empty(&ctx.peers->reliable_queue));
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_TX_FAILED);
    uint32_t delay = 0;
    EXPECT_FALSE(xgl_transport_next_timeout(&ctx, 200, &delay));
}

TEST_F(TransportRegressionTest, RetransmissionBusyHasPositiveRetryDeadline) {
    Init(1);
    ASSERT_EQ(Send(), XGL_OK);
    spy.busy_data_attempts = 2;
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 200), XGL_OK);
    uint32_t delay = 0;
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 200, &delay));
    ASSERT_GT(delay, 0U);
    EXPECT_EQ(spy.busy_data_attempts, 1U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 200 + delay - 1U), XGL_OK);
    EXPECT_EQ(spy.busy_data_attempts, 1U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 200 + delay), XGL_OK);
    EXPECT_EQ(spy.busy_data_attempts, 0U);
    uint32_t next_delay = 0;
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 200 + delay, &next_delay));
    ASSERT_GT(next_delay, 0U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 200 + delay + next_delay),
              XGL_OK);
    EXPECT_EQ(retries, 1U);
    EXPECT_FALSE(ctx.peers->failed);
}

TEST_F(TransportRegressionTest,
       FastRetransmitBusyDeadlineDoesNotWaitForOriginalRto) {
    Init(1);
    ctx.default_timeout_ms = 10;
    ASSERT_EQ(Send(), XGL_OK);
    auto* packet =
        xgl_reliable_find_packet_number(&ctx.peers->reliable_queue, 0, 2);
    ASSERT_NE(packet, nullptr);
    const uint8_t bitmap[] = {0};
    uint8_t value[16] = {}, extensions[20] = {};
    size_t value_len = 0, extensions_len = 0;
    ASSERT_EQ(xgl_wire_encode_sack_ext_value(value, sizeof(value), 0, bitmap,
                                             sizeof(bitmap), &value_len),
              XGL_OK);
    ASSERT_EQ(xgl_wire_encode_ext(extensions, sizeof(extensions),
                                  XGL_WIRE_EXT_SACK, value, value_len,
                                  &extensions_len),
              XGL_OK);
    xgl_packet_t feedback = {};
    feedback.source_id = 2;
    feedback.target_id = 1;
    feedback.packet_type = XGL_PACKET_TYPE_ACK;
    feedback.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
    feedback.reliable = XGL_RELIABILITY_ACK_ONLY;
    feedback.extensions = extensions;
    feedback.extensions_len = extensions_len;
    spy.busy_data_attempts = 1;
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &feedback), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 110), XGL_OK);
    EXPECT_TRUE(packet->retry_pending);
    uint32_t delay = 0;
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 110, &delay));
    EXPECT_EQ(delay, 10U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 120), XGL_OK);
    EXPECT_EQ(retries, 1U);
    EXPECT_EQ(packet->send_timestamp, 120U);
}

TEST_F(TransportRegressionTest,
       UnsupportedCodecCannotSilentlyTransmitPlainBytes) {
#if !XGL_FEATURE_CODEC
    Init();
    const uint8_t payload = 'a';
    xgl_tx_data_t tx = {};
    tx.target_id = 2;
    tx.data = &payload;
    tx.data_len = 1;
    tx.compression_id = 1;
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx), XGL_ERR_UNSUPPORTED);
    EXPECT_EQ(spy.send_count, 0);
#endif
}

TEST(XglTransportTest, ReliableSendQueuesPacketAndAckReleasesWindow) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 100;

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    EXPECT_EQ(spy.send_count, 2);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    ASSERT_EQ(spy.sent_packets.size(), 2U);
    EXPECT_EQ(spy.sent_packets[0].data_type, XGL_TEST_CONTROL_HELLO);
    EXPECT_EQ(spy.sent_packets[1].data_type, 1U);
    EXPECT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 1);
    EXPECT_FALSE(xgl_transport_can_send_to(&ctx, 2, 0, 0));

    uint8_t ack_value[16] = {};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {{/* gap */ 0, /* length */ 1}};
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(ack_value, sizeof(ack_value),
                                                  0, 0, ranges, 1,
                                                  &ack_value_len),
              XGL_OK);

    uint8_t ack_ext[32] = {};
    size_t ack_ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ack_ext, sizeof(ack_ext),
                                  XGL_WIRE_EXT_ACK_RANGE, ack_value,
                                  ack_value_len, &ack_ext_len),
              XGL_OK);

    xgl_packet_data_t ack_data = {};
    ack_data.data_len = 0;
    ack_data.data = nullptr;
    xgl_packet_t ack_packet = {};
    ack_packet.source_id = 2;
    ack_packet.target_id = 1;
    ack_packet.packet_type = XGL_PACKET_TYPE_ACK;
    ack_packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
    ack_packet.data_type = 0;
    ack_packet.reliable = XGL_RELIABILITY_ACK_ONLY;
    ack_packet.priority = 7;
    ack_packet.data = &ack_data;
    ack_packet.extensions = ack_ext;
    ack_packet.extensions_len = ack_ext_len;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 0);
    EXPECT_TRUE(xgl_transport_can_send_to(&ctx, 2, 0, 0));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableSendDoesNotTransmitWhenQueueAdmissionFails) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 100;

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    ASSERT_EQ(xgl_reliable_remove_packet_number(&peer->reliable_queue, 0, 2),
              XGL_OK);
    xgl_window_reset(&peer->tx_window);

    xgl_allocator_t failing_allocator = {};
    failing_allocator.ctx = nullptr;
    failing_allocator.alloc = always_fail_malloc;
    failing_allocator.free = always_fail_free;
    peer->reliable_queue.allocator = &failing_allocator;
    const int send_count_before = spy.send_count;

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_ERR_NO_MEMORY);
    EXPECT_EQ(spy.send_count, send_count_before);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 0U);
    EXPECT_TRUE(xgl_window_can_send_packet_number(&peer->tx_window));

    peer->reliable_queue.allocator = xgl_test_host_allocator();
    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, AckRangeExtensionReleasesMultipleReliablePackets) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    for (int i = 0; i < 4; ++i) {
        xgl_tx_data_t tx_data = {};
        tx_data.target_id = 2;
        tx_data.data_type = 1;
        tx_data.data = payload;
        tx_data.data_len = sizeof(payload);
        tx_data.reliable = true;
        tx_data.priority = 0;
        tx_data.timeout_ms = 100;
        ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    }

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 4U);

    uint8_t ack_value[16] = {};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {{/* gap */ 0, /* length */ 4}};
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(ack_value, sizeof(ack_value),
                                                  3, 0, ranges, 1,
                                                  &ack_value_len),
              XGL_OK);

    uint8_t ext[32] = {};
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext, sizeof(ext), XGL_WIRE_EXT_ACK_RANGE,
                                  ack_value, ack_value_len, &ext_len),
              XGL_OK);

    xgl_packet_data_t ack_ext_data = {};
    ack_ext_data.data_len = 0;
    ack_ext_data.data = nullptr;
    xgl_packet_t ack_packet = {};
    ack_packet.source_id = 2;
    ack_packet.target_id = 1;
    ack_packet.packet_type = XGL_PACKET_TYPE_ACK;
    ack_packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
    ack_packet.data_type = 0;
    ack_packet.reliable = XGL_RELIABILITY_ACK_ONLY;
    ack_packet.priority = 7;
    ack_packet.data = &ack_ext_data;
    ack_packet.extensions = ext;
    ack_packet.extensions_len = ext_len;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 0U);
    EXPECT_TRUE(xgl_transport_can_send_to(&ctx, 2, 0, 0));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, SackExtensionFastRetransmitsMissingPacketAndKeepsHole) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    for (int i = 0; i < 4; ++i) {
        xgl_tx_data_t tx_data = {};
        tx_data.target_id = 2;
        tx_data.data_type = 1;
        tx_data.data = payload;
        tx_data.data_len = sizeof(payload);
        tx_data.reliable = true;
        tx_data.priority = 0;
        tx_data.timeout_ms = 100;
        ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    }

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 4U);
    const int send_count_before_sack = spy.send_count;

    uint8_t sack_value[16] = {};
    size_t sack_value_len = 0;
    const uint8_t bitmap[] = {
        0x0BU}; /* Received packets 0, 1, and 3; packet 2 is the hole. */
    ASSERT_EQ(xgl_wire_encode_sack_ext_value(sack_value, sizeof(sack_value), 0,
                                             bitmap, sizeof(bitmap),
                                             &sack_value_len),
              XGL_OK);

    uint8_t ext[32] = {};
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext, sizeof(ext), XGL_WIRE_EXT_SACK,
                                  sack_value, sack_value_len, &ext_len),
              XGL_OK);

    xgl_packet_data_t sack_ext_data = {};
    sack_ext_data.data_len = 0;
    sack_ext_data.data = nullptr;
    xgl_packet_t sack_packet = {};
    sack_packet.source_id = 2;
    sack_packet.target_id = 1;
    sack_packet.packet_type = XGL_PACKET_TYPE_ACK;
    sack_packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
    sack_packet.data_type = 0;
    sack_packet.reliable = XGL_RELIABILITY_ACK_ONLY;
    sack_packet.priority = 7;
    sack_packet.data = &sack_ext_data;
    sack_packet.extensions = ext;
    sack_packet.extensions_len = ext_len;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &sack_packet), XGL_OK);
    EXPECT_EQ(spy.send_count, send_count_before_sack);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, ctx.current_time_ms), XGL_OK);
    EXPECT_EQ(spy.send_count, send_count_before_sack + 1);
    EXPECT_EQ(spy.last_packet.packet_number, 2U);
    EXPECT_EQ(tx_retries, 1U);

    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 1U);
    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 2, 2),
              nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&peer->reliable_queue, 0, 2),
              nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&peer->reliable_queue, 1, 2),
              nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&peer->reliable_queue, 3, 2),
              nullptr);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, EmptySackBitmapRetransmitsBasePacket) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 100;
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    const int send_count_before_sack = spy.send_count;

    uint8_t sack_value[16] = {};
    size_t sack_value_len = 0;
    const uint8_t bitmap[] = {0x00U};
    ASSERT_EQ(xgl_wire_encode_sack_ext_value(sack_value, sizeof(sack_value), 0,
                                             bitmap, sizeof(bitmap),
                                             &sack_value_len),
              XGL_OK);

    uint8_t ext[32] = {};
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext, sizeof(ext), XGL_WIRE_EXT_SACK,
                                  sack_value, sack_value_len, &ext_len),
              XGL_OK);

    xgl_packet_data_t sack_ext_data = {};
    sack_ext_data.data_len = 0;
    sack_ext_data.data = nullptr;
    xgl_packet_t sack_packet = {};
    sack_packet.source_id = 2;
    sack_packet.target_id = 1;
    sack_packet.packet_type = XGL_PACKET_TYPE_ACK;
    sack_packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
    sack_packet.data_type = 0;
    sack_packet.reliable = XGL_RELIABILITY_ACK_ONLY;
    sack_packet.priority = 7;
    sack_packet.data = &sack_ext_data;
    sack_packet.extensions = ext;
    sack_packet.extensions_len = ext_len;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &sack_packet), XGL_OK);
    EXPECT_EQ(spy.send_count, send_count_before_sack);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, ctx.current_time_ms), XGL_OK);
    EXPECT_EQ(spy.send_count, send_count_before_sack + 1);
    EXPECT_EQ(spy.last_packet.packet_number, 0U);
    EXPECT_EQ(tx_retries, 1U);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 1U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest,
     ProductionAckWithUnknownExtensionDoesNotUseLegacyAckNumber) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 100;
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 1U);

    const uint8_t value[] = {0xAA};
    uint8_t ext[8] = {};
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext, sizeof(ext), 250U, value, sizeof(value),
                                  &ext_len),
              XGL_OK);

    xgl_packet_data_t ack_ext_data = {};
    ack_ext_data.data_len = ext_len;
    ack_ext_data.data = ext;
    xgl_packet_t ack_packet = {};
    ack_packet.source_id = 2;
    ack_packet.target_id = 1;
    ack_packet.packet_type = XGL_PACKET_TYPE_ACK;
    ack_packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
    ack_packet.data_type = 0;
    ack_packet.reliable = XGL_RELIABILITY_ACK_ONLY;
    ack_packet.priority = 7;
    ack_packet.data = &ack_ext_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 1U);
    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 0, 2),
              nullptr);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableSendUsesMonotonicPacketNumbersPastEightBitWrap) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 100;

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);

    xgl_reliable_clear(&peer->reliable_queue);
    xgl_window_reset(&peer->tx_window);
    peer->tx_window.send_base_packet_number = 254;
    peer->tx_window.next_packet_number = 254;
    spy.sent_packets.clear();
    spy.send_count = 0;

    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    }

    ASSERT_EQ(spy.sent_packets.size(), 3U);
    EXPECT_EQ(spy.sent_packets[0].packet_number, 254U);
    EXPECT_EQ(spy.sent_packets[1].packet_number, 255U);
    EXPECT_EQ(spy.sent_packets[2].packet_number, 256U);
    EXPECT_EQ(spy.sent_packets[2].packet_number, 256U);

    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 254, 2),
              nullptr);
    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 255, 2),
              nullptr);
    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 256, 2),
              nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 3U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, AckFromUnexpectedSourceIsRejected) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 100;

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 1);

    xgl_packet_data_t ack_data = {};
    ack_data.data_len = 0;
    ack_data.data = nullptr;
    xgl_packet_t ack_packet = {};
    ack_packet.source_id = 3;
    ack_packet.target_id = 1;
    ack_packet.packet_type = XGL_PACKET_TYPE_ACK;
    ack_packet.data_type = 0;
    ack_packet.reliable = XGL_RELIABILITY_ACK_ONLY;
    ack_packet.priority = 7;
    ack_packet.data = &ack_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet),
              XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 1);
    EXPECT_FALSE(xgl_transport_can_send_to(&ctx, 2, 0, 0));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, PeerWindowFullDoesNotBlockDifferentTarget) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_to_2 = {};
    tx_to_2.target_id = 2;
    tx_to_2.data_type = 1;
    tx_to_2.data = payload;
    tx_to_2.data_len = sizeof(payload);
    tx_to_2.reliable = true;
    tx_to_2.priority = 0;
    tx_to_2.timeout_ms = 100;
    xgl_tx_data_t tx_to_3 = tx_to_2;
    tx_to_3.target_id = 3;

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_2), XGL_OK);
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_2), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_3), XGL_OK);
    EXPECT_EQ(spy.send_count, 4);
    ASSERT_NE(ctx.peers, nullptr);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableQueuesArePeerScoped) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_to_2 = {};
    tx_to_2.target_id = 2;
    tx_to_2.data_type = 1;
    tx_to_2.data = payload;
    tx_to_2.data_len = sizeof(payload);
    tx_to_2.reliable = true;
    tx_to_2.priority = 0;
    tx_to_2.timeout_ms = 100;
    xgl_tx_data_t tx_to_3 = tx_to_2;
    tx_to_3.target_id = 3;

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_2), XGL_OK);
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_3), XGL_OK);

    ASSERT_NE(ctx.peers, nullptr);
    size_t peers_with_queued_packets = 0;
    for (xgl_transport_peer_state_t* peer = ctx.peers; peer != nullptr;
         peer = peer->next) {
        if (xgl_reliable_get_count(&peer->reliable_queue) == 1) {
            peers_with_queued_packets++;
        }
    }

    EXPECT_EQ(peers_with_queued_packets, 2U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, HelloCreatesExactPeerWithoutDeliveringToApplication) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t dummy = 0;
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = 0;
    packet_data.data = &dummy;
    xgl_packet_t hello_packet = {};
    hello_packet.source_id = 2;
    hello_packet.target_id = 1;
    hello_packet.packet_type = XGL_PACKET_TYPE_CONTROL;
    hello_packet.data_type = XGL_TEST_CONTROL_HELLO;
    hello_packet.reliable = XGL_RELIABILITY_NONE;
    hello_packet.priority = 7;
    hello_packet.data = &packet_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &hello_packet), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    EXPECT_EQ(find_peer(&ctx, 2)->connection_id, 0U);
    EXPECT_EQ(find_peer(&ctx, 2)->session_epoch, 0U);
    EXPECT_EQ(rx_tracker.receive_count, 0);
    EXPECT_EQ(spy.send_count, 0);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest,
     ApplicationDataTypeCollidingWithControlValueIsDelivered) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'a', 'p', 'p'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t data_packet = {};
    data_packet.source_id = 2;
    data_packet.target_id = 1;
    data_packet.packet_type = XGL_PACKET_TYPE_DATA;
    data_packet.data_type = XGL_TEST_CONTROL_HELLO;
    data_packet.reliable = XGL_RELIABILITY_NONE;
    data_packet.priority = 0;
    data_packet.data = &packet_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &data_packet), XGL_OK);
    EXPECT_EQ(find_peer(&ctx, 2), nullptr);
    ASSERT_EQ(rx_tracker.receive_count, 1);
    ASSERT_EQ(rx_tracker.payloads.size(), 1U);
    EXPECT_EQ(rx_tracker.payloads[0], std::vector<uint8_t>({'a', 'p', 'p'}));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, FirstReliableDataCreatesExactPeerState) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'n', 'e', 'w'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t data_packet = {};
    data_packet.packet_type = XGL_PACKET_TYPE_DATA;
    data_packet.source_id = 4;
    data_packet.target_id = 1;
    data_packet.data_type = 1;
    data_packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    data_packet.priority = 0;
    data_packet.data = &packet_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &data_packet), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 4), nullptr);
    EXPECT_EQ(find_peer(&ctx, 4)->connection_id, 0U);
    EXPECT_EQ(find_peer(&ctx, 4)->session_epoch, 0U);
    EXPECT_EQ(rx_tracker.receive_count, 1);
    EXPECT_EQ(spy.send_count, 1);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, OutOfOrderReliablePacketSendsSackForExpectedSequence) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'o', 'o', 'o'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.source_id = 2;
    packet.target_id = 1;
    packet.packet_number = 2;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.priority = 0;
    packet.data = &packet_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet),
              XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(rx_tracker.receive_count, 0);
    ASSERT_EQ(spy.send_count, 1);
    expect_sack_packet_for_base(spy, 0U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, OutOfOrderReliablePacketIsBufferedAndDeliveredAfterGap) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload0[] = {'p', '0'};
    const uint8_t payload1[] = {'p', '1'};
    const uint8_t payload2[] = {'p', '2'};
    xgl_packet_data_t data0 = {};
    data0.data_len = sizeof(payload0);
    data0.data = payload0;
    xgl_packet_data_t data1 = {};
    data1.data_len = sizeof(payload1);
    data1.data = payload1;
    xgl_packet_data_t data2 = {};
    data2.data_len = sizeof(payload2);
    data2.data = payload2;

    xgl_packet_t packet = {};
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.source_id = 2;
    packet.target_id = 1;
    packet.packet_number = 0;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.priority = 0;
    packet.data = &data0;

    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    ASSERT_EQ(rx_tracker.receive_count, 1);

    packet.packet_number = 2;
    packet.packet_number = 2;
    packet.data = &data2;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(rx_tracker.receive_count, 1);
    ASSERT_EQ(spy.send_count, 2);
    expect_sack_packet_for_base(spy, 1U);

    packet.packet_number = 1;
    packet.packet_number = 1;
    packet.data = &data1;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    ASSERT_EQ(rx_tracker.receive_count, 3);
    ASSERT_EQ(rx_tracker.payloads.size(), 3U);
    EXPECT_EQ(rx_tracker.payloads[0], std::vector<uint8_t>({'p', '0'}));
    EXPECT_EQ(rx_tracker.payloads[1], std::vector<uint8_t>({'p', '1'}));
    EXPECT_EQ(rx_tracker.payloads[2], std::vector<uint8_t>({'p', '2'}));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableReceiveSendsAckRangeExtensionForPacketNumber) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'n', '2'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.source_id = 2;
    packet.target_id = 1;
    packet.packet_number = 0;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.priority = 0;
    packet.data = &packet_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(rx_tracker.receive_count, 1);
    ASSERT_EQ(spy.send_count, 1);
    EXPECT_EQ(spy.last_packet.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_NE(spy.last_packet.flags & XGL_WIRE_FLAG_HAS_EXTENSIONS, 0U);
    EXPECT_EQ(spy.last_packet.packet_number, 0U);
    ASSERT_EQ(spy.sent_extensions.size(), 1U);

    xgl_wire_ext_cursor_t cursor;
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor,
                                       spy.sent_extensions.back().data(),
                                       spy.sent_extensions.back().size()),
              XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_ACK_RANGE);

    xgl_wire_ack_range_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_ack_range_view(ext.value, ext.len, &view),
              XGL_OK);
    EXPECT_EQ(view.largest_ack, 0U);
    EXPECT_EQ(view.ack_delay_us, 0U);
    ASSERT_EQ(view.range_count, 1U);
    xgl_wire_ack_range_t range = {};
    ASSERT_EQ(xgl_wire_ack_range_at(&view, 0U, &range), XGL_OK);
    EXPECT_EQ(range.gap, 0U);
    EXPECT_EQ(range.length, 1U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, AckRangeUsesHeaderExtensionNotPayload) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    lower_layer.ctx = &spy;
    lower_layer.send = spy_send;
    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    RxTracker rx_tracker;
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    xgl_transport_ctx_t ctx = {};
    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.source_id = 2;
    packet.target_id = 1;
    packet.packet_number = 0;
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.data = &packet_data;

    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    ASSERT_EQ(spy.send_count, 1);
    EXPECT_EQ(spy.last_packet.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_TRUE(spy.sent_payloads.back().empty());
    ASSERT_FALSE(spy.sent_extensions.back().empty());

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor,
                                       spy.sent_extensions.back().data(),
                                       spy.sent_extensions.back().size()),
              XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_ACK_RANGE);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableReceiveAckPreservesConnectionScope) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'s', 'c'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.source_id = 2;
    packet.target_id = 1;
    packet.connection_id = 0x11223344U;
    packet.packet_number = 0;
    packet.session_epoch = 0x01020304U;
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.data = &packet_data;

    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);

    ASSERT_EQ(spy.send_count, 1);
    EXPECT_EQ(spy.last_packet.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(spy.last_packet.connection_id, packet.connection_id);
    EXPECT_EQ(spy.last_packet.session_epoch, packet.session_epoch);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableReceiveIsolatesPacketNumbersByConnectionScope) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload_a[] = {'c', '1'};
    const uint8_t payload_b[] = {'c', '2'};
    xgl_packet_data_t data_a = {};
    data_a.data_len = sizeof(payload_a);
    data_a.data = payload_a;
    xgl_packet_data_t data_b = {};
    data_b.data_len = sizeof(payload_b);
    data_b.data = payload_b;

    xgl_packet_t packet = {};
    packet.source_id = 2;
    packet.target_id = 1;
    packet.connection_id = 11;
    packet.packet_number = 0;
    packet.session_epoch = 100;
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.priority = 0;
    packet.data = &data_a;

    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);

    packet.connection_id = 22;
    packet.session_epoch = 200;
    packet.data = &data_b;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);

    ASSERT_EQ(rx_tracker.receive_count, 2);
    ASSERT_EQ(rx_tracker.payloads.size(), 2U);
    EXPECT_EQ(rx_tracker.payloads[0], std::vector<uint8_t>({'c', '1'}));
    EXPECT_EQ(rx_tracker.payloads[1], std::vector<uint8_t>({'c', '2'}));
    EXPECT_EQ(spy.send_count, 2);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableReceiveRejectsPacketNumberOutsideReceiveWindow) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'n'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.source_id = 2;
    packet.target_id = 1;
    packet.packet_number = 0;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.priority = 0;
    packet.data = &packet_data;

    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(rx_tracker.receive_count, 1);
    EXPECT_EQ(spy.send_count, 1);

    packet.packet_number = 256;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet),
              XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(rx_tracker.receive_count, 1);
    ASSERT_EQ(spy.send_count, 2);
    expect_sack_packet_for_base(spy, 1U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, SingleFrameSendChecksLowerPolicyBeforeSubmission) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower = {};
    xgl_packet_tx_endpoint_init(&lower, &spy, spy_send);
    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower, &stats, &tx_retries);
    config.max_frame_size = 128;
    lower.check_data = [](void* user, xgl_handle_t, const xgl_tx_data_t* tx,
                          bool need_hello) {
        const auto* observed = static_cast<const LowerLayerSpy*>(user);
        EXPECT_EQ(observed->send_count, 0);
        EXPECT_FALSE(need_hello);
        EXPECT_EQ(tx->target_id, 2U);
        return tx->data_len > 32U ? XGL_ERR_BUFFER_TOO_SMALL : XGL_OK;
    };
    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    constexpr size_t payload_budget = 32U;
    const uint8_t payload[payload_budget + 1U] = {};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 7;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(spy.send_count, 0);

    tx_data.data_len = payload_budget;
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_EQ(spy.send_count, 1);
    EXPECT_EQ(spy.sent_payloads.back().size(), payload_budget);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableTimeoutRetransmitsThroughLowerLayer) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 100;

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_EQ(spy.send_count, 2);

    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    xgl_reliable_packet_t* queued = xgl_reliable_find_packet_number(
        &find_peer(&ctx, 2)->reliable_queue, 0, 2);
    ASSERT_NE(queued, nullptr);
    queued->send_timestamp = 100;

    EXPECT_EQ(xgl_transport_run(&ctx, nullptr, 201), XGL_OK);
    EXPECT_EQ(spy.send_count, 3);
    EXPECT_EQ(tx_retries, 1);

    queued = xgl_reliable_find_packet_number(
        &find_peer(&ctx, 2)->reliable_queue, 0, 2);
    ASSERT_NE(queued, nullptr);
    EXPECT_EQ(queued->retry_count, 1);
    EXPECT_EQ(queued->send_timestamp, 201U);
    EXPECT_EQ(spy.last_packet.target_id, 2);
    EXPECT_EQ(spy.last_packet.packet_number, 0U);
    EXPECT_EQ(spy.last_packet.reliable, XGL_RELIABILITY_ACK_ELICITING);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableSendUsesIndependentWindowsPerConnectionScope) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 1;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t first = {};
    first.target_id = 2;
    first.data_type = 1;
    first.data = payload;
    first.data_len = sizeof(payload);
    first.reliable = true;
    first.priority = 0;
    first.timeout_ms = 100;
    first.connection_id = 11;
    first.session_epoch = 100;
    xgl_tx_data_t second = first;
    second.connection_id = 22;
    second.session_epoch = 200;

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &first), XGL_OK);
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &second), XGL_OK);

    EXPECT_NE(find_peer_scope_for_test(&ctx, 2, 11, 100), nullptr);
    EXPECT_NE(find_peer_scope_for_test(&ctx, 2, 22, 200), nullptr);

    ASSERT_GE(spy.sent_packets.size(), 4U);
    EXPECT_EQ(spy.sent_packets[1].connection_id, 11U);
    EXPECT_EQ(spy.sent_packets[1].session_epoch, 100U);
    EXPECT_EQ(spy.sent_packets[3].connection_id, 22U);
    EXPECT_EQ(spy.sent_packets[3].session_epoch, 200U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, UnreliablePacketsWithSameSequenceAreDelivered) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.source_id = 2;
    packet.target_id = 1;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_NONE;
    packet.priority = 0;
    packet.data = &packet_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(rx_tracker.receive_count, 2);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableDuplicateDetectionIsScopedBySource) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.source_id = 2;
    packet.target_id = 1;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.priority = 0;
    packet.data = &packet_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);

    packet.source_id = 3;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);

    EXPECT_EQ(rx_tracker.receive_count, 2);
    EXPECT_EQ(spy.send_count, 3);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, PeerIdleTimeoutRetainsReceiveDuplicateHistory) {
    LowerLayerSpy spy;
    RxTracker tracker;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.peer_idle_timeout_ms = 1000;
    config.rx_callback = spy_receive;
    config.callback_user_data = &tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    /* Create a peer with accepted reliable DATA history. */
    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_packet_data_t packet_data = {};
    packet_data.data_len = sizeof(payload);
    packet_data.data = payload;
    xgl_packet_t packet = {};
    packet.source_id = 2;
    packet.target_id = 1;
    packet.data_type = 1;
    packet.reliable = XGL_RELIABILITY_ACK_ELICITING;
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.data = &packet_data;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);

    /* Set last_active_ms to a past time and run transport beyond timeout */
    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    peer->last_active_ms = 100U;
    EXPECT_EQ(xgl_transport_run(&ctx, nullptr, 5000U), XGL_OK);

    /* Accepted packet numbers survive idle time and still suppress duplicates.
     */
    ASSERT_EQ(find_peer(&ctx, 2), peer);
    EXPECT_EQ(peer->rx_next_packet_number, 1U);
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(peer->rx_next_packet_number, 1U);
    EXPECT_EQ(tracker.receive_count, 1);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, PeerWithPendingReliableNotReclaimed) {
    LowerLayerSpy spy;
    xgl_packet_tx_endpoint_t lower_layer = {};
    xgl_packet_tx_endpoint_init(&lower_layer, &spy, spy_send);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config =
        make_transport_config(&lower_layer, &stats, &tx_retries);
    config.peer_idle_timeout_ms = 1000;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    /* Send a reliable packet to create a peer with pending reliable queue */
    const uint8_t payload[] = {'h', 'i'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 100;
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);

    /* Set last_active_ms to past and run beyond timeout */
    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    peer->last_active_ms = 100U;
    EXPECT_EQ(xgl_transport_run(&ctx, nullptr, 5000U), XGL_OK);

    /* Peer should NOT be reclaimed because reliable queue has pending packets
     */
    EXPECT_NE(find_peer(&ctx, 2), nullptr);

    xgl_transport_destroy(&ctx);
}

TEST_F(TransportRegressionTest,
       DefaultScopeAndExplicitScopesHaveIndependentWindows) {
    Init(1);
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Send(11, 0), XGL_OK);
    ASSERT_EQ(Send(11, 100), XGL_OK);
    EXPECT_EQ(Send(), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(Send(11, 0), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(Send(11, 100), XGL_ERR_WINDOW_FULL);
    ASSERT_EQ(Ack(0, 1, 11, 100), XGL_OK);
    EXPECT_FALSE(xgl_transport_can_send_to(&ctx, 2, 0, 0));
    EXPECT_FALSE(xgl_transport_can_send_to(&ctx, 2, 11, 0));
    EXPECT_TRUE(xgl_transport_can_send_to(&ctx, 2, 11, 100));
}

TEST_F(TransportRegressionTest, HelloAndResetRespectTerminalScopeOwnership) {
    Init(2);
    ASSERT_EQ(Send(11, 100), XGL_OK);
    auto* peer = ctx.peers;
    auto* packet = xgl_reliable_find_packet_number(&peer->reliable_queue, 0, 2);
    ASSERT_NE(packet, nullptr);
    xgl_packet_t control = {};
    control.source_id = 2;
    control.target_id = 1;
    control.connection_id = 11;
    control.session_epoch = 100;
    control.packet_type = XGL_PACKET_TYPE_CONTROL;
    control.data_type = XGL_TRANSPORT_CONTROL_HELLO;
    for (unsigned attempt = 0; attempt < 3U; ++attempt) {
        ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &control), XGL_OK);
        EXPECT_EQ(xgl_reliable_find_packet_number(&peer->reliable_queue, 0, 2),
                  packet);
        EXPECT_EQ(peer->tx_window.next_packet_number, 1U);
    }
    control.data_type = XGL_TRANSPORT_CONTROL_RESET;
    for (unsigned attempt = 0; attempt < 3U; ++attempt) {
        ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &control), XGL_OK);
        EXPECT_TRUE(peer->failed);
        EXPECT_TRUE(xgl_reliable_is_empty(&peer->reliable_queue));
        EXPECT_EQ(peer->tx_window.next_packet_number, 1U);
        EXPECT_EQ(Send(11, 100), XGL_ERR_ACK_TIMEOUT);
    }
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_CANCELLED}));
    control.session_epoch = 99;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &control),
              XGL_ERR_NOT_FOUND);
    EXPECT_EQ(ctx.peers, peer);
    EXPECT_EQ(peer->next, nullptr);
}

TEST_F(TransportRegressionTest,
       AckControlAndUnreliableDoNotConsumeDataNumbers) {
    Init(4);
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Receive(0, 'a'), XGL_OK);
    ASSERT_EQ(Receive(1, 'b'), XGL_OK);
    ASSERT_EQ(Receive(0, 'a'), XGL_OK);
    xgl_tx_data_t unreliable = {};
    const uint8_t byte = 'u';
    unreliable.target_id = 2;
    unreliable.data = &byte;
    unreliable.data_len = 1;
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &unreliable), XGL_OK);
    ASSERT_EQ(Send(), XGL_OK);
    std::vector<uint32_t> reliable_numbers;
    for (const auto& packet : spy.sent_packets) {
        if (packet.packet_type == XGL_PACKET_TYPE_DATA &&
            packet.reliable == XGL_RELIABILITY_ACK_ELICITING) {
            reliable_numbers.push_back(packet.packet_number);
        } else {
            EXPECT_EQ(packet.packet_number, 0U);
        }
    }
    EXPECT_EQ(reliable_numbers, (std::vector<uint32_t>{0, 1}));
    EXPECT_EQ(ctx.peers->tx_window.next_packet_number, 2U);
}

TEST_F(TransportRegressionTest, GlobalTxPacketBudgetIsReleasedByExactAck) {
    Init(4);
    ctx.max_tx_packets = 1;
    ASSERT_EQ(Send(), XGL_OK);
    EXPECT_EQ(Send(11, 100), XGL_ERR_WINDOW_FULL);
    auto* other = find_peer_scope_for_test(&ctx, 2, 11, 100);
    EXPECT_EQ(other, nullptr);
    ASSERT_EQ(Ack(0, 1), XGL_OK);
    EXPECT_EQ(Send(11, 100), XGL_OK);
}

TEST_F(TransportRegressionTest,
       GlobalOutOfOrderBudgetDoesNotAcknowledgeDroppedBytes) {
    Init(4);
    ctx.max_rx_buffered_packets = 1;
    ASSERT_EQ(Receive(1, 'b'), XGL_OK);
    EXPECT_EQ(Receive(2, 'c'), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(ctx.peers->rx_buffered_count, 1U);
    ASSERT_EQ(Receive(0, 'a'), XGL_OK);
    EXPECT_EQ(accepted_payloads.size(), 2U);
    EXPECT_EQ(ctx.peers->rx_next_packet_number, 2U);
    EXPECT_EQ(Receive(2, 'c'), XGL_OK);
    EXPECT_EQ(accepted_payloads.size(), 3U);
}

TEST_F(TransportRegressionTest, TypedBoundaryOnlyExposesReceive) {
    Init();
    xgl_packet_rx_endpoint_t iface = {};
    ASSERT_EQ(xgl_transport_get_rx_endpoint(&ctx, &iface), XGL_OK);
    EXPECT_NE(iface.receive, nullptr);
    EXPECT_EQ(iface.ctx, &ctx);
}

TEST_F(TransportRegressionTest, DelayedOldAckCannotCompleteNewEpoch) {
    Init(1);
    ASSERT_EQ(Send(11, 100), XGL_OK);
    xgl_packet_t reset = {};
    reset.source_id = 2;
    reset.target_id = 1;
    reset.connection_id = 11;
    reset.session_epoch = 100;
    reset.packet_type = XGL_PACKET_TYPE_CONTROL;
    reset.data_type = XGL_TRANSPORT_CONTROL_RESET;
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &reset), XGL_OK);
    ASSERT_EQ(Send(11, 101), XGL_OK);
    auto* current = find_peer_scope_for_test(&ctx, 2, 11, 101);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(Ack(0, 1, 11, 100), XGL_ERR_ACK_TIMEOUT);
    EXPECT_EQ(xgl_reliable_get_count(&current->reliable_queue), 1U);
    EXPECT_EQ(current->tx_window.send_base_packet_number, 0U);
    EXPECT_EQ(Ack(0, 1, 11, 101), XGL_OK);
    EXPECT_TRUE(xgl_reliable_is_empty(&current->reliable_queue));
}

TEST_F(TransportRegressionTest,
       ExplicitCloseCancelsOnceAndReusesBoundedPeerSlot) {
    Init(1);
    ctx.max_peers = 1;
    ASSERT_EQ(Send(11, 100), XGL_OK);
    ASSERT_NE(ctx.peers, nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1U);
    EXPECT_EQ(xgl_transport_close_scope(&ctx, nullptr, 2, 11, 99),
              XGL_ERR_NOT_FOUND);
    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(xgl_transport_close_scope(&ctx, nullptr, 2, 11, 100), XGL_OK);
    EXPECT_EQ(ctx.peers, nullptr);
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_CANCELLED}));
    EXPECT_EQ(xgl_transport_close_scope(&ctx, nullptr, 2, 11, 100),
              XGL_ERR_NOT_FOUND);
    EXPECT_EQ(errors.size(), 1U);
    ASSERT_EQ(Send(11, 101), XGL_OK);
    EXPECT_EQ(Ack(0, 1, 11, 100), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1U);
    EXPECT_EQ(Ack(0, 1, 11, 101), XGL_OK);
}

TEST_F(TransportRegressionTest, CloseAlreadyFailedScopeDoesNotReportTwice) {
    Init(1);
    ASSERT_EQ(Send(11, 100), XGL_OK);
    transport_fail_peer(&ctx, nullptr, ctx.peers, XGL_ERR_TX_FAILED);
    ASSERT_EQ(errors.size(), 1U);
    ASSERT_EQ(xgl_transport_close_scope(&ctx, nullptr, 2, 11, 100), XGL_OK);
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_TX_FAILED}));
    EXPECT_EQ(ctx.peers, nullptr);
}

TEST_F(TransportRegressionTest, InitRequiresExplicitAllocatorAndCapacities) {
    auto config = make_transport_config(&lower, &stats, &retries);
    config.allocator = nullptr;
    EXPECT_EQ(xgl_transport_init(&ctx, &config), XGL_ERR_NULL_POINTER);
    config.allocator = xgl_test_host_allocator();
    config.max_peers = 0;
    EXPECT_EQ(xgl_transport_init(&ctx, &config), XGL_ERR_INVALID_PARAM);
    config.max_peers = 1;
    config.max_tx_packets = 0;
    EXPECT_EQ(xgl_transport_init(&ctx, &config), XGL_ERR_INVALID_PARAM);
    config.max_tx_packets = 1;
    config.max_rx_buffered_packets = 0;
    config.window_size = 2;
    EXPECT_EQ(xgl_transport_init(&ctx, &config), XGL_ERR_INVALID_PARAM);
    config.window_size = 1;
    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);
    initialized = true;
}

TEST_F(TransportRegressionTest, IdlePeerWithoutReliableHistoryCanBeReclaimed) {
    Init();
    ctx.max_peers = 1;
    ctx.peer_idle_timeout_ms = 1000;
    xgl_packet_t hello = {};
    hello.source_id = 2;
    hello.target_id = 1;
    hello.packet_type = XGL_PACKET_TYPE_CONTROL;
    hello.data_type = XGL_TEST_CONTROL_HELLO;
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &hello), XGL_OK);
    ASSERT_NE(ctx.peers, nullptr);
    uint32_t timeout = 0;
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 100, &timeout));
    EXPECT_EQ(timeout, 1000U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 1100), XGL_OK);
    EXPECT_EQ(ctx.peers, nullptr);
    EXPECT_TRUE(xgl_transport_can_send_to(&ctx, 3, 0, 1));
}

TEST_F(TransportRegressionTest, DuplicateSackPreservesRetryBudgetAndRto) {
    Init();
    ctx.max_retry_count = 3;
    for (unsigned i = 0; i < 4; ++i) {
        ASSERT_EQ(Send(), XGL_OK);
    }
    ASSERT_EQ(Sack(0x0e), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 100), XGL_OK);
    ASSERT_EQ(retries, 1U);
    const int sends_after_first = spy.send_count;
    for (unsigned i = 0; i < 4; ++i) {
        EXPECT_EQ(Sack(0x0e), XGL_OK);
    }
    EXPECT_EQ(spy.send_count, sends_after_first);
    ASSERT_FALSE(ctx.peers->failed);
    EXPECT_TRUE(errors.empty());
    uint32_t delay = 0;
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 100, &delay));
    EXPECT_EQ(delay, 200U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 299), XGL_OK);
    EXPECT_EQ(retries, 1U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 300), XGL_OK);
    EXPECT_EQ(retries, 2U);
    EXPECT_EQ(Sack(0x0e), XGL_OK);
    EXPECT_EQ(retries, 2U);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 700), XGL_OK);
    EXPECT_EQ(retries, 3U);
    EXPECT_FALSE(ctx.peers->failed);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 1500), XGL_ERR_ACK_TIMEOUT);
    EXPECT_TRUE(ctx.peers->failed);
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_ACK_TIMEOUT}));
}

TEST_F(TransportRegressionTest,
       NewSackProgressCanRetransmitAnotherUntriedHole) {
    Init();
    for (unsigned i = 0; i < 4; ++i) {
        ASSERT_EQ(Send(), XGL_OK);
    }
    ASSERT_EQ(Sack(0x02), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 100), XGL_OK);
    EXPECT_EQ(retries, 1U);
    ASSERT_EQ(Sack(0x0a), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 100), XGL_OK);
    EXPECT_EQ(retries, 2U);
    ASSERT_FALSE(ctx.peers->failed);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 2U);
    EXPECT_EQ(spy.sent_packets.back().packet_number, 2U);
}

TEST_F(TransportRegressionTest, EmptySackCannotRepeatSuccessfulFastRetry) {
    Init(1);
    ASSERT_EQ(Send(), XGL_OK);
    ASSERT_EQ(Sack(0), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 100), XGL_OK);
    EXPECT_EQ(retries, 1U);
    EXPECT_EQ(Sack(0), XGL_OK);
    EXPECT_EQ(Sack(0), XGL_OK);
    EXPECT_EQ(retries, 1U);
    EXPECT_FALSE(ctx.peers->failed);
    EXPECT_TRUE(errors.empty());
}

TEST_F(TransportRegressionTest, SackCountsSuccessfulRetryBeforeLaterBusy) {
    Init();
    for (unsigned i = 0; i < 4; ++i) {
        ASSERT_EQ(Send(), XGL_OK);
    }
    spy.selected_error_packet = 2;
    spy.selected_error = XGL_ERR_BUSY;
    ASSERT_EQ(Sack(0x0a), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 100), XGL_OK);
    EXPECT_EQ(retries, 1U);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 2U);
    spy.selected_error_packet = UINT32_MAX;
    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 200), XGL_OK);
    EXPECT_EQ(retries, 2U);
    EXPECT_FALSE(ctx.peers->failed);
}

TEST_F(TransportRegressionTest, SackCountsSuccessfulRetryBeforeLaterHardError) {
    Init();
    for (unsigned i = 0; i < 4; ++i) {
        ASSERT_EQ(Send(), XGL_OK);
    }
    spy.selected_error_packet = 2;
    spy.selected_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(Sack(0x0a), XGL_OK);
    EXPECT_EQ(xgl_transport_run(&ctx, nullptr, 100), XGL_ERR_TX_FAILED);
    EXPECT_EQ(retries, 1U);
    EXPECT_TRUE(ctx.peers->failed);
    EXPECT_EQ(errors, (std::vector<xgl_error_t>{XGL_ERR_TX_FAILED}));
}

TEST_F(TransportRegressionTest,
       RejectedFirstPacketReleasesPeerWithoutIdleTimer) {
    Init();
    ctx.max_peers = 1;
    ctx.peer_idle_timeout_ms = 0;
    EXPECT_EQ(Receive(100, 'a'), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(ctx.peers, nullptr);
    EXPECT_TRUE(accepted_payloads.empty());
    EXPECT_EQ(Send(1, 1), XGL_OK);
}

TEST_F(TransportRegressionTest, RejectionPreservesAlreadyAcceptedPeerHistory) {
    Init();
    ctx.max_peers = 1;
    ASSERT_EQ(Receive(0, 'a'), XGL_OK);
    EXPECT_EQ(Receive(100, 'b'), XGL_ERR_WINDOW_FULL);
    ASSERT_NE(ctx.peers, nullptr);
    EXPECT_TRUE(ctx.peers->rx_has_packet_number_state);
    EXPECT_EQ(Receive(0, 'a'), XGL_OK);
    EXPECT_EQ(accepted_payloads.size(), 1U);
    EXPECT_EQ(Send(1, 1), XGL_ERR_NO_MEMORY);
}

TEST_F(TransportRegressionTest,
       AckTimeoutPrefersOverrideThenMeasuredThenConfig) {
    Init();
    ctx.default_timeout_ms = 450;
    const uint8_t payload = 'a';
    xgl_tx_data_t tx = {};
    tx.target_id = 2;
    tx.data = &payload;
    tx.data_len = 1;
    tx.reliable = true;
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx), XGL_OK);
    uint32_t delay = 0;
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 100, &delay));
    EXPECT_EQ(delay, 450U);
    ctx.current_time_ms = 150;
    ASSERT_EQ(Ack(0, 1), XGL_OK);
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx), XGL_OK);
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 150, &delay));
#if XGL_FEATURE_ADAPTIVE_RTO
    EXPECT_EQ(delay, 150U);
#else
    EXPECT_EQ(delay, 450U);
#endif
    ctx.current_time_ms = 200;
    ASSERT_EQ(Ack(1, 1), XGL_OK);
    tx.timeout_ms = 700;
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx), XGL_OK);
    ASSERT_TRUE(xgl_transport_next_timeout(&ctx, 200, &delay));
    EXPECT_EQ(delay, 700U);
}
