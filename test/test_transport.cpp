/**
 * \file            test_transport.cpp
 * \brief           Transport layer unit tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/internal/xgl_transport.h>
#include <xgl/internal/xgl_reliable.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/internal/xgl_window.h>
#include <cstdlib>
#include <cstring>
#include <vector>

template <typename T>
concept HasLegacyAckHandler = requires(T value) {
    value.ack_handler;
};

static_assert(!HasLegacyAckHandler<xgl_transport_ctx_t>,
              "xgl_transport_ctx_t must not retain legacy 8-bit ACK handler state");

namespace {

struct LowerLayerSpy {
    int send_count = 0;
    xgl_packet_t last_packet = {};
    std::vector<xgl_packet_t> sent_packets;
    std::vector<std::vector<uint8_t>> sent_payloads;
    std::vector<std::vector<uint8_t>> sent_extensions;
};

struct RxTracker {
    int receive_count = 0;
    std::vector<std::vector<uint8_t>> payloads;
};

constexpr uint8_t kTransportControlHello = 0x0E;
constexpr uint8_t kTransportControlReset = 0x0F;

static xgl_error_t spy_send(void* ctx, xgl_handle_t handle, void* data) {
    (void)handle;

    auto* spy = static_cast<LowerLayerSpy*>(ctx);
    auto* packet = static_cast<xgl_packet_t*>(data);
    if (spy == nullptr || packet == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }

    spy->send_count++;
    spy->last_packet = *packet;
    spy->sent_packets.push_back(*packet);
    if (packet->data != nullptr && packet->data->data != nullptr && packet->data->data_len > 0U) {
        spy->sent_payloads.emplace_back(packet->data->data,
                                        packet->data->data + packet->data->data_len);
    } else {
        spy->sent_payloads.emplace_back();
    }
    if (packet->extensions != nullptr && packet->extensions_len > 0U) {
        spy->sent_extensions.emplace_back(packet->extensions,
                                         packet->extensions + packet->extensions_len);
    } else {
        spy->sent_extensions.emplace_back();
    }
    return XGL_OK;
}

static void* always_fail_malloc(size_t size) {
    (void)size;
    return nullptr;
}

static void always_fail_free(void* ptr) {
    std::free(ptr);
}

static void spy_receive(xgl_handle_t handle,
                        uint16_t source_id,
                        uint8_t data_type,
                        const uint8_t* data,
                        size_t len,
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

static xgl_transport_config_t make_transport_config(xgl_layer_interface_t* lower_layer,
                                                    xgl_layer_stats_t* stats,
                                                    uint64_t* tx_retries) {
    xgl_transport_config_t config = {};
    config.local_id = 1;
    config.max_retry_count = 3;
    config.default_timeout_ms = 100;
    config.window_size = 1;
    config.enable_fragmentation = false;
    config.max_frame_size = 128;
    config.lower_layer = lower_layer;
    config.stats = stats;
    config.tx_retries = tx_retries;
    return config;
}

static xgl_transport_peer_state_t* find_peer(xgl_transport_ctx_t* ctx, uint16_t peer_id) {
    for (xgl_transport_peer_state_t* peer = ctx->peers; peer != nullptr; peer = peer->next) {
        if (peer->peer_id == peer_id) {
            return peer;
        }
    }
    return nullptr;
}

static xgl_transport_peer_state_t* find_peer_scope_for_test(xgl_transport_ctx_t* ctx,
                                                            uint16_t peer_id,
                                                            uint32_t connection_id,
                                                            uint32_t session_epoch) {
    for (xgl_transport_peer_state_t* peer = ctx->peers; peer != nullptr; peer = peer->next) {
        if (peer->peer_id == peer_id &&
            peer->has_connection_scope &&
            peer->connection_id == connection_id &&
            peer->session_epoch == session_epoch) {
            return peer;
        }
    }
    return nullptr;
}

static void expect_sack_packet_for_base(const LowerLayerSpy& spy, uint32_t base_packet) {
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

    uint32_t decoded_base = 0;
    uint8_t bitmap[8] = {};
    size_t bitmap_len = 0;
    ASSERT_EQ(xgl_wire_decode_sack_ext_value(ext.value,
                                             ext.len,
                                             &decoded_base,
                                             bitmap,
                                             sizeof(bitmap),
                                             &bitmap_len),
              XGL_OK);
    EXPECT_EQ(decoded_base, base_packet);
    ASSERT_GT(bitmap_len, 0U);
}

}  // namespace

TEST(XglTransportTest, ReliableSendQueuesPacketAndAckReleasesWindow) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    EXPECT_EQ(spy.send_count, 2);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    ASSERT_EQ(spy.sent_packets.size(), 2U);
    EXPECT_EQ(spy.sent_packets[0].data_type, kTransportControlHello);
    EXPECT_EQ(spy.sent_packets[0].session_id, find_peer(&ctx, 2)->session_id);
    EXPECT_EQ(spy.sent_packets[1].data_type, 1U);
    EXPECT_EQ(spy.last_packet.session_id, find_peer(&ctx, 2)->session_id);
    EXPECT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 1);
    EXPECT_FALSE(xgl_transport_can_send(&ctx));

    uint8_t ack_value[16] = {};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {
        {.gap = 0, .length = 1}
    };
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(ack_value,
                                                  sizeof(ack_value),
                                                  0,
                                                  0,
                                                  ranges,
                                                  1,
                                                  &ack_value_len),
              XGL_OK);

    uint8_t ack_ext[32] = {};
    size_t ack_ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ack_ext,
                                  sizeof(ack_ext),
                                  XGL_WIRE_EXT_ACK_RANGE,
                                  ack_value,
                                  ack_value_len,
                                  &ack_ext_len),
              XGL_OK);

    xgl_packet_data_t ack_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = nullptr,
        .owned_data = nullptr
    };
    xgl_packet_t ack_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = find_peer(&ctx, 2)->session_id,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .priority = 7,
        .data = &ack_data,
        .extensions = ack_ext,
        .extensions_len = ack_ext_len
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 0);
    EXPECT_TRUE(xgl_transport_can_send(&ctx));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableSendDoesNotTransmitWhenQueueAdmissionFails) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    ASSERT_EQ(xgl_reliable_remove_packet_number(&peer->reliable_queue, 0, 2), XGL_OK);
    xgl_window_reset(&peer->tx_window);
    xgl_window_reset(&ctx.window);

    xgl_allocator_t failing_allocator = {
        .malloc = always_fail_malloc,
        .free = always_fail_free,
        .user_data = nullptr
    };
    peer->reliable_queue.allocator = &failing_allocator;
    const int send_count_before = spy.send_count;

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_ERR_NO_MEMORY);
    EXPECT_EQ(spy.send_count, send_count_before);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 0U);
    EXPECT_TRUE(xgl_window_can_send_packet_number(&peer->tx_window));

    peer->reliable_queue.allocator = nullptr;
    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, AckRangeExtensionReleasesMultipleReliablePackets) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    for (int i = 0; i < 4; ++i) {
        xgl_tx_data_t tx_data = {
            .target_id = 2,
            .data_type = 1,
            .data = payload,
            .data_len = sizeof(payload),
            .reliable = true,
            .priority = 0,
            .timeout_ms = 100
        };
        ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    }

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 4U);

    uint8_t ack_value[16] = {};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {
        {.gap = 0, .length = 4}
    };
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(ack_value,
                                                  sizeof(ack_value),
                                                  3,
                                                  0,
                                                  ranges,
                                                  1,
                                                  &ack_value_len),
              XGL_OK);

    uint8_t ext[32] = {};
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext,
                                  sizeof(ext),
                                  XGL_WIRE_EXT_ACK_RANGE,
                                  ack_value,
                                  ack_value_len,
                                  &ext_len),
              XGL_OK);

    xgl_packet_data_t ack_ext_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = nullptr,
        .owned_data = nullptr
    };
    xgl_packet_t ack_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = peer->session_id,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .priority = 7,
        .data = &ack_ext_data,
        .extensions = ext,
        .extensions_len = ext_len
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 0U);
    EXPECT_TRUE(xgl_transport_can_send(&ctx));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, SackExtensionFastRetransmitsMissingPacketAndKeepsHole) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    for (int i = 0; i < 4; ++i) {
        xgl_tx_data_t tx_data = {
            .target_id = 2,
            .data_type = 1,
            .data = payload,
            .data_len = sizeof(payload),
            .reliable = true,
            .priority = 0,
            .timeout_ms = 100
        };
        ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    }

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 4U);
    const int send_count_before_sack = spy.send_count;

    uint8_t sack_value[16] = {};
    size_t sack_value_len = 0;
    const uint8_t bitmap[] = {0x0BU};  // Received packets 0, 1, and 3; packet 2 is the hole.
    ASSERT_EQ(xgl_wire_encode_sack_ext_value(sack_value,
                                             sizeof(sack_value),
                                             0,
                                             bitmap,
                                             sizeof(bitmap),
                                             &sack_value_len),
              XGL_OK);

    uint8_t ext[32] = {};
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext,
                                  sizeof(ext),
                                  XGL_WIRE_EXT_SACK,
                                  sack_value,
                                  sack_value_len,
                                  &ext_len),
              XGL_OK);

    xgl_packet_data_t sack_ext_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = nullptr,
        .owned_data = nullptr
    };
    xgl_packet_t sack_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = peer->session_id,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .priority = 7,
        .data = &sack_ext_data,
        .extensions = ext,
        .extensions_len = ext_len
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &sack_packet), XGL_OK);
    EXPECT_EQ(spy.send_count, send_count_before_sack + 1);
    EXPECT_EQ(spy.last_packet.packet_number, 2U);
    EXPECT_EQ(tx_retries, 1U);

    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 1U);
    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 2, 2), nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&peer->reliable_queue, 0, 2), nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&peer->reliable_queue, 1, 2), nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&peer->reliable_queue, 3, 2), nullptr);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, EmptySackBitmapRetransmitsBasePacket) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    const int send_count_before_sack = spy.send_count;

    uint8_t sack_value[16] = {};
    size_t sack_value_len = 0;
    const uint8_t bitmap[] = {0x00U};
    ASSERT_EQ(xgl_wire_encode_sack_ext_value(sack_value,
                                             sizeof(sack_value),
                                             0,
                                             bitmap,
                                             sizeof(bitmap),
                                             &sack_value_len),
              XGL_OK);

    uint8_t ext[32] = {};
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext,
                                  sizeof(ext),
                                  XGL_WIRE_EXT_SACK,
                                  sack_value,
                                  sack_value_len,
                                  &ext_len),
              XGL_OK);

    xgl_packet_data_t sack_ext_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = nullptr,
        .owned_data = nullptr
    };
    xgl_packet_t sack_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = peer->session_id,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .priority = 7,
        .data = &sack_ext_data,
        .extensions = ext,
        .extensions_len = ext_len
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &sack_packet), XGL_OK);
    EXPECT_EQ(spy.send_count, send_count_before_sack + 1);
    EXPECT_EQ(spy.last_packet.packet_number, 0U);
    EXPECT_EQ(tx_retries, 1U);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 1U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ProductionAckWithUnknownExtensionDoesNotUseLegacyAckNumber) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 1U);

    const uint8_t value[] = {0xAA};
    uint8_t ext[8] = {};
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext,
                                  sizeof(ext),
                                  XGL_WIRE_EXT_ROUTE,
                                  value,
                                  sizeof(value),
                                  &ext_len),
              XGL_OK);

    xgl_packet_data_t ack_ext_data = {
        .ref_count = 1,
        .data_len = ext_len,
        .data = ext,
        .owned_data = nullptr
    };
    xgl_packet_t ack_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = peer->session_id,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .flags = XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .priority = 7,
        .data = &ack_ext_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 1U);
    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 0, 2), nullptr);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableSendUsesMonotonicPacketNumbersPastEightBitWrap) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);

    xgl_reliable_clear(&peer->reliable_queue);
    xgl_window_reset(&peer->tx_window);
    xgl_window_reset(&ctx.window);
    peer->tx_window.send_base_packet_number = 254;
    peer->tx_window.next_packet_number = 254;
    ctx.window.send_base_packet_number = 254;
    ctx.window.next_packet_number = 254;
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

    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 254, 2), nullptr);
    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 255, 2), nullptr);
    EXPECT_NE(xgl_reliable_find_packet_number(&peer->reliable_queue, 256, 2), nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&peer->reliable_queue), 3U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, AckFromUnexpectedSourceIsRejected) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 1);

    xgl_packet_data_t ack_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = nullptr,
        .owned_data = nullptr
    };
    xgl_packet_t ack_packet = {
        .source_id = 3,
        .target_id = 1,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .priority = 7,
        .data = &ack_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 1);
    EXPECT_FALSE(xgl_transport_can_send(&ctx));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, PeerWindowFullDoesNotBlockDifferentTarget) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_to_2 = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };
    xgl_tx_data_t tx_to_3 = tx_to_2;
    tx_to_3.target_id = 3;

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_2), XGL_OK);
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_2), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_3), XGL_OK);
    EXPECT_EQ(spy.send_count, 4);
    ASSERT_NE(ctx.peers, nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.reliable_queue), 0);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableQueuesArePeerScoped) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_to_2 = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };
    xgl_tx_data_t tx_to_3 = tx_to_2;
    tx_to_3.target_id = 3;

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_2), XGL_OK);
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_to_3), XGL_OK);

    ASSERT_NE(ctx.peers, nullptr);
    size_t peers_with_queued_packets = 0;
    for (xgl_transport_peer_state_t* peer = ctx.peers; peer != nullptr; peer = peer->next) {
        if (xgl_reliable_get_count(&peer->reliable_queue) == 1) {
            peers_with_queued_packets++;
        }
    }

    EXPECT_EQ(peers_with_queued_packets, 2U);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.reliable_queue), 0);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, AckWithWrongSessionIsRejected) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_NE(ctx.peers, nullptr);
    uint16_t wrong_session = static_cast<uint16_t>(ctx.peers->session_id + 1U);

    xgl_packet_data_t ack_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = nullptr,
        .owned_data = nullptr
    };
    xgl_packet_t ack_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = wrong_session,
        .packet_type = XGL_PACKET_TYPE_ACK,
        .data_type = 0,
        .reliable = XGL_RELIABILITY_ACK_ONLY,
        .priority = 7,
        .data = &ack_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.peers->reliable_queue), 1);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, HelloSetsPeerSessionWithoutDeliveringToApplication) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t dummy = 0;
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = &dummy,
        .owned_data = nullptr
    };
    xgl_packet_t hello_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 9,
        .packet_type = XGL_PACKET_TYPE_CONTROL,
        .data_type = kTransportControlHello,
        .reliable = XGL_RELIABILITY_NONE,
        .priority = 7,
        .data = &packet_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &hello_packet), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    EXPECT_EQ(find_peer(&ctx, 2)->session_id, 9U);
    EXPECT_EQ(rx_tracker.receive_count, 0);
    EXPECT_EQ(spy.send_count, 0);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ApplicationDataTypeCollidingWithControlValueIsDelivered) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'a', 'p', 'p'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t data_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 0,
        .packet_type = XGL_PACKET_TYPE_DATA,
        .data_type = kTransportControlHello,
        .reliable = XGL_RELIABILITY_NONE,
        .priority = 0,
        .data = &packet_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &data_packet), XGL_OK);
    EXPECT_EQ(find_peer(&ctx, 2), nullptr);
    ASSERT_EQ(rx_tracker.receive_count, 1);
    ASSERT_EQ(rx_tracker.payloads.size(), 1U);
    EXPECT_EQ(rx_tracker.payloads[0], std::vector<uint8_t>({'a', 'p', 'p'}));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ResetUpdatesSessionAndClearsPeerReliableState) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };
    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    ASSERT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 1U);

    const uint8_t dummy = 0;
    xgl_packet_data_t reset_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = &dummy,
        .owned_data = nullptr
    };
    xgl_packet_t reset_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 11,
        .packet_type = XGL_PACKET_TYPE_CONTROL,
        .data_type = kTransportControlReset,
        .reliable = XGL_RELIABILITY_NONE,
        .priority = 7,
        .data = &reset_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &reset_packet), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    EXPECT_EQ(find_peer(&ctx, 2)->session_id, 11U);
    EXPECT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 0U);
    EXPECT_TRUE(xgl_window_can_send(&find_peer(&ctx, 2)->tx_window));
    EXPECT_TRUE(xgl_transport_can_send(&ctx));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableDataWithStaleSessionIsRejectedBeforeAckOrDelivery) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t dummy = 0;
    xgl_packet_data_t control_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = &dummy,
        .owned_data = nullptr
    };
    xgl_packet_t hello_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 5,
        .packet_type = XGL_PACKET_TYPE_CONTROL,
        .data_type = kTransportControlHello,
        .reliable = XGL_RELIABILITY_NONE,
        .priority = 7,
        .data = &control_data
    };
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &hello_packet), XGL_OK);

    const uint8_t payload[] = {'o', 'l', 'd'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t stale_data_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 4,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 0,
        .data = &packet_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &stale_data_packet), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(rx_tracker.receive_count, 0);
    EXPECT_EQ(spy.send_count, 0);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, FirstReliableDataWithSessionCreatesPeerState) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'n', 'e', 'w'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t data_packet = {
        .source_id = 4,
        .target_id = 1,
        .session_id = 13,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 0,
        .data = &packet_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &data_packet), XGL_OK);
    ASSERT_NE(find_peer(&ctx, 4), nullptr);
    EXPECT_EQ(find_peer(&ctx, 4)->session_id, 13U);
    EXPECT_EQ(rx_tracker.receive_count, 1);
    EXPECT_EQ(spy.send_count, 1);
    EXPECT_EQ(spy.last_packet.session_id, 13U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ResetClearsInFlightFragmentReassembly) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.enable_fragmentation = true;
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);
    ASSERT_NE(ctx.fragment_mgr, nullptr);

    const uint8_t first_fragment[] = {'a', 'b', 'c', 'd'};
    uint8_t first_fragment_ext_value[12] = {};
    size_t first_fragment_ext_value_len = 0;
    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(first_fragment_ext_value,
                                                 sizeof(first_fragment_ext_value),
                                                 3,
                                                 0,
                                                 6,
                                                 &first_fragment_ext_value_len),
              XGL_OK);
    uint8_t first_fragment_ext[16] = {};
    size_t first_fragment_ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(first_fragment_ext,
                                  sizeof(first_fragment_ext),
                                  XGL_WIRE_EXT_FRAGMENT,
                                  first_fragment_ext_value,
                                  first_fragment_ext_value_len,
                                  &first_fragment_ext_len),
              XGL_OK);
    xgl_packet_data_t first_data = {
        .ref_count = 1,
        .data_len = sizeof(first_fragment),
        .data = first_fragment,
        .owned_data = nullptr
    };
    xgl_packet_t first_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 5,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_NONE,
        .fragment = true,
        .priority = 0,
        .data = &first_data,
        .extensions = first_fragment_ext,
        .extensions_len = first_fragment_ext_len
    };

    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &first_packet), XGL_OK);
    ASSERT_EQ(xgl_fragment_get_reassembly_count(ctx.fragment_mgr), 1U);

    const uint8_t dummy = 0;
    xgl_packet_data_t reset_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = &dummy,
        .owned_data = nullptr
    };
    xgl_packet_t reset_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 6,
        .packet_type = XGL_PACKET_TYPE_CONTROL,
        .data_type = kTransportControlReset,
        .reliable = XGL_RELIABILITY_NONE,
        .priority = 7,
        .data = &reset_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &reset_packet), XGL_OK);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(ctx.fragment_mgr), 0U);

    uint8_t stale_second_fragment[] = {
        3, 1, 2, 4, 0,
        'e', 'f'
    };
    xgl_packet_data_t second_data = {
        .ref_count = 1,
        .data_len = sizeof(stale_second_fragment),
        .data = stale_second_fragment,
        .owned_data = nullptr
    };
    xgl_packet_t stale_packet = first_packet;
    stale_packet.session_id = 5;
    stale_packet.packet_number = 1;
    stale_packet.data = &second_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &stale_packet), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(rx_tracker.receive_count, 0);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, OutOfOrderReliablePacketSendsSackForExpectedSequence) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'o', 'o', 'o'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 5,
        .packet_number = 2,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 0,
        .data = &packet_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(rx_tracker.receive_count, 0);
    ASSERT_EQ(spy.send_count, 1);
    expect_sack_packet_for_base(spy, 0U);
    EXPECT_EQ(spy.last_packet.session_id, 5U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, OutOfOrderReliablePacketIsBufferedAndDeliveredAfterGap) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload0[] = {'p', '0'};
    const uint8_t payload1[] = {'p', '1'};
    const uint8_t payload2[] = {'p', '2'};
    xgl_packet_data_t data0 = {.ref_count = 1, .data_len = sizeof(payload0), .data = payload0};
    xgl_packet_data_t data1 = {.ref_count = 1, .data_len = sizeof(payload1), .data = payload1};
    xgl_packet_data_t data2 = {.ref_count = 1, .data_len = sizeof(payload2), .data = payload2};

    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 5,
        .packet_number = 0,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 0,
        .data = &data0
    };

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
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'n', '2'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 5,
        .packet_number = 0,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 0,
        .data = &packet_data
    };

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

    uint32_t largest_ack = 0;
    uint32_t ack_delay_us = 0;
    xgl_wire_ack_range_t ranges[1] = {};
    size_t range_count = 0;
    ASSERT_EQ(xgl_wire_decode_ack_range_ext_value(ext.value,
                                                  ext.len,
                                                  &largest_ack,
                                                  &ack_delay_us,
                                                  ranges,
                                                  1,
                                                  &range_count),
              XGL_OK);
    EXPECT_EQ(largest_ack, 0U);
    EXPECT_EQ(ack_delay_us, 0U);
    ASSERT_EQ(range_count, 1U);
    EXPECT_EQ(ranges[0].gap, 0U);
    EXPECT_EQ(ranges[0].length, 1U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, AckRangeUsesHeaderExtensionNotPayload) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    lower_layer.ctx = &spy;
    lower_layer.send = spy_send;
    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    RxTracker rx_tracker;
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    xgl_transport_ctx_t ctx = {};
    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 5,
        .packet_number = 0,
        .packet_type = XGL_PACKET_TYPE_DATA,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .data = &packet_data
    };

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
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'s', 'c'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 5,
        .connection_id = 0x11223344U,
        .packet_number = 0,
        .session_epoch = 0x01020304U,
        .packet_type = XGL_PACKET_TYPE_DATA,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .data = &packet_data
    };

    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);

    ASSERT_EQ(spy.send_count, 1);
    EXPECT_EQ(spy.last_packet.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(spy.last_packet.connection_id, packet.connection_id);
    EXPECT_EQ(spy.last_packet.session_epoch, packet.session_epoch);
    EXPECT_EQ(spy.last_packet.session_id, packet.session_id);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableReceiveIsolatesPacketNumbersByConnectionScope) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload_a[] = {'c', '1'};
    const uint8_t payload_b[] = {'c', '2'};
    xgl_packet_data_t data_a = {
        .ref_count = 1,
        .data_len = sizeof(payload_a),
        .data = payload_a,
        .owned_data = nullptr
    };
    xgl_packet_data_t data_b = {
        .ref_count = 1,
        .data_len = sizeof(payload_b),
        .data = payload_b,
        .owned_data = nullptr
    };

    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .connection_id = 11,
        .packet_number = 0,
        .session_epoch = 100,
        .packet_type = XGL_PACKET_TYPE_DATA,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 0,
        .data = &data_a
    };

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
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'n'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 5,
        .packet_number = 0,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 0,
        .data = &packet_data
    };

    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(rx_tracker.receive_count, 1);
    EXPECT_EQ(spy.send_count, 1);

    packet.packet_number = 256;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(rx_tracker.receive_count, 1);
    ASSERT_EQ(spy.send_count, 2);
    expect_sack_packet_for_base(spy, 1U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, FragmentedSendUsesFragmentExtensionNotPayloadPrefix) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.enable_fragmentation = true;
    config.max_frame_size = 48;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z', '0', '1', '2', '3', '4', '5',
        '6', '7', '8', '9', 'a', 'b', 'c', 'd'
    };
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = false,
        .priority = 0,
        .timeout_ms = 100
    };

    EXPECT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_GT(spy.sent_packets.size(), 1U);
    ASSERT_EQ(spy.sent_packets.size(), spy.sent_extensions.size());
    ASSERT_EQ(spy.sent_packets.size(), spy.sent_payloads.size());

    size_t observed_payload_bytes = 0;
    for (size_t i = 0; i < spy.sent_packets.size(); ++i) {
        EXPECT_TRUE(spy.sent_packets[i].fragment);
        ASSERT_FALSE(spy.sent_extensions[i].empty());
        xgl_wire_ext_cursor_t cursor = {};
        ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor,
                                           spy.sent_extensions[i].data(),
                                           spy.sent_extensions[i].size()),
                  XGL_OK);
        xgl_wire_ext_t ext = {};
        ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
        EXPECT_EQ(ext.type, XGL_WIRE_EXT_FRAGMENT);

        uint32_t message_id = 0;
        uint32_t fragment_offset = 0;
        uint32_t message_len = 0;
        ASSERT_EQ(xgl_wire_decode_fragment_ext_value(ext.value,
                                                     ext.len,
                                                     &message_id,
                                                     &fragment_offset,
                                                     &message_len),
                  XGL_OK);
        EXPECT_EQ(message_len, sizeof(payload));
        ASSERT_LT(fragment_offset, sizeof(payload));
        ASSERT_FALSE(spy.sent_payloads[i].empty());
        EXPECT_EQ(spy.sent_payloads[i][0], payload[fragment_offset]);
        observed_payload_bytes += spy.sent_payloads[i].size();
    }

    EXPECT_EQ(observed_payload_bytes, sizeof(payload));
    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, FragmentReceiveUsesFragmentExtensionMetadata) {
    RxTracker tracker;
    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(nullptr, &stats, &tx_retries);
    config.enable_fragmentation = true;
    config.rx_callback = spy_receive;
    config.callback_user_data = &tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t first_payload[] = {'A', 'B'};
    const uint8_t second_payload[] = {'C', 'D'};
    uint8_t first_ext_value[12] = {};
    uint8_t second_ext_value[12] = {};
    uint8_t first_ext[14] = {};
    uint8_t second_ext[14] = {};
    size_t value_len = 0;
    size_t first_ext_len = 0;
    size_t second_ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(first_ext_value,
                                                 sizeof(first_ext_value),
                                                 99,
                                                 0,
                                                 4,
                                                 &value_len),
              XGL_OK);
    ASSERT_EQ(xgl_wire_encode_ext(first_ext,
                                  sizeof(first_ext),
                                  XGL_WIRE_EXT_FRAGMENT,
                                  first_ext_value,
                                  value_len,
                                  &first_ext_len),
              XGL_OK);
    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(second_ext_value,
                                                 sizeof(second_ext_value),
                                                 99,
                                                 2,
                                                 4,
                                                 &value_len),
              XGL_OK);
    ASSERT_EQ(xgl_wire_encode_ext(second_ext,
                                  sizeof(second_ext),
                                  XGL_WIRE_EXT_FRAGMENT,
                                  second_ext_value,
                                  value_len,
                                  &second_ext_len),
              XGL_OK);

    xgl_packet_data_t first_data = {
        .ref_count = 1,
        .data_len = sizeof(first_payload),
        .data = first_payload,
        .owned_data = nullptr
    };
    xgl_packet_t first_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 0,
        .connection_id = 7,
        .packet_number = 10,
        .session_epoch = 3,
        .version = 0,
        .packet_type = XGL_PACKET_TYPE_DATA,
        .flags = XGL_WIRE_FLAG_FRAGMENTED | XGL_WIRE_FLAG_HAS_EXTENSIONS,
        .data_type = 1,
        .fragment = true,
        .data = &first_data,
        .extensions = first_ext,
        .extensions_len = first_ext_len
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &first_packet), XGL_OK);
    EXPECT_EQ(tracker.receive_count, 0);

    xgl_packet_data_t second_data = {
        .ref_count = 1,
        .data_len = sizeof(second_payload),
        .data = second_payload,
        .owned_data = nullptr
    };
    xgl_packet_t second_packet = first_packet;
    second_packet.packet_number = 11;
    second_packet.data = &second_data;
    second_packet.extensions = second_ext;
    second_packet.extensions_len = second_ext_len;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &second_packet), XGL_OK);
    EXPECT_EQ(tracker.receive_count, 1);
    EXPECT_EQ(stats.rx_packets, 1U);
    EXPECT_EQ(stats.rx_bytes, 4U);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableTimeoutRetransmitsThroughLowerLayer) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_EQ(spy.send_count, 2);

    ASSERT_NE(find_peer(&ctx, 2), nullptr);
    xgl_reliable_packet_t* queued =
        xgl_reliable_find_packet_number(&find_peer(&ctx, 2)->reliable_queue, 0, 2);
    ASSERT_NE(queued, nullptr);
    queued->send_timestamp = 100;

    EXPECT_EQ(xgl_transport_run(&ctx, nullptr, 201), XGL_OK);
    EXPECT_EQ(spy.send_count, 3);
    EXPECT_EQ(tx_retries, 1);

    queued = xgl_reliable_find_packet_number(&find_peer(&ctx, 2)->reliable_queue, 0, 2);
    ASSERT_NE(queued, nullptr);
    EXPECT_EQ(queued->retry_count, 1);
    EXPECT_EQ(queued->send_timestamp, 201U);
    EXPECT_EQ(spy.last_packet.target_id, 2);
    EXPECT_EQ(spy.last_packet.packet_number, 0U);
    EXPECT_EQ(spy.last_packet.reliable, 1);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableSendUsesIndependentWindowsPerConnectionScope) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.window_size = 1;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_tx_data_t first = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100,
        .connection_id = 11,
        .session_epoch = 100
    };
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

TEST(XglTransportTest, ReliableFragmentTimeoutRetransmitsWithFragmentExtension) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.enable_fragmentation = true;
    config.max_frame_size = 48;
    config.window_size = 8;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z', '0', '1', '2', '3', '4', '5',
        '6', '7', '8', '9', 'a', 'b', 'c', 'd'
    };
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = payload,
        .data_len = sizeof(payload),
        .reliable = true,
        .priority = 0,
        .timeout_ms = 100
    };

    ASSERT_EQ(xgl_transport_send(&ctx, nullptr, &tx_data), XGL_OK);
    ASSERT_GT(spy.sent_packets.size(), 2U);
    ASSERT_FALSE(spy.sent_extensions[1].empty());
    const std::vector<uint8_t> first_fragment_ext = spy.sent_extensions[1];
    const std::vector<uint8_t> first_fragment_payload = spy.sent_payloads[1];
    const uint32_t first_fragment_packet_number = spy.sent_packets[1].packet_number;

    xgl_transport_peer_state_t* peer = find_peer(&ctx, 2);
    ASSERT_NE(peer, nullptr);
    xgl_list_node_t* node = nullptr;
    XGL_LIST_FOR_EACH(&peer->reliable_queue.wait_ack_list, node) {
        xgl_reliable_packet_t* packet =
            XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        packet->send_timestamp = 0;
    }
    xgl_reliable_packet_t* queued =
        xgl_reliable_find_packet_number(&peer->reliable_queue, first_fragment_packet_number, 2);
    ASSERT_NE(queued, nullptr);
    queued->send_timestamp = 100;

    ASSERT_EQ(xgl_transport_run(&ctx, nullptr, 201), XGL_OK);
    ASSERT_EQ(tx_retries, 1U);
    ASSERT_FALSE(spy.sent_extensions.back().empty());

    EXPECT_EQ(spy.sent_extensions.back(), first_fragment_ext);
    EXPECT_EQ(spy.sent_payloads.back(), first_fragment_payload);
    EXPECT_TRUE(spy.last_packet.fragment);
    EXPECT_EQ(spy.last_packet.flags & XGL_WIRE_FLAG_HAS_EXTENSIONS,
              XGL_WIRE_FLAG_HAS_EXTENSIONS);

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor,
                                       spy.sent_extensions.back().data(),
                                       spy.sent_extensions.back().size()),
              XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_FRAGMENT);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ResetClearsOnlyMatchingFragmentReassemblyScope) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.enable_fragmentation = true;
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t peer2_part[] = {'a', 'b'};
    const uint8_t peer3_part[] = {'x', 'y'};
    const uint8_t peer3_tail[] = {'z', 'w'};

    uint8_t peer2_ext_value[12] = {};
    size_t peer2_ext_value_len = 0;
    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(peer2_ext_value,
                                                 sizeof(peer2_ext_value),
                                                 10,
                                                 0,
                                                 4,
                                                 &peer2_ext_value_len),
              XGL_OK);
    uint8_t peer2_ext[16] = {};
    size_t peer2_ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(peer2_ext,
                                  sizeof(peer2_ext),
                                  XGL_WIRE_EXT_FRAGMENT,
                                  peer2_ext_value,
                                  peer2_ext_value_len,
                                  &peer2_ext_len),
              XGL_OK);

    uint8_t peer3_ext_value[12] = {};
    size_t peer3_ext_value_len = 0;
    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(peer3_ext_value,
                                                 sizeof(peer3_ext_value),
                                                 20,
                                                 0,
                                                 4,
                                                 &peer3_ext_value_len),
              XGL_OK);
    uint8_t peer3_ext[16] = {};
    size_t peer3_ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(peer3_ext,
                                  sizeof(peer3_ext),
                                  XGL_WIRE_EXT_FRAGMENT,
                                  peer3_ext_value,
                                  peer3_ext_value_len,
                                  &peer3_ext_len),
              XGL_OK);

    xgl_packet_data_t peer2_data = {
        .ref_count = 1,
        .data_len = sizeof(peer2_part),
        .data = peer2_part,
        .owned_data = nullptr
    };
    xgl_packet_t peer2_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 0,
        .connection_id = 11,
        .session_epoch = 100,
        .data_type = 1,
        .fragment = true,
        .data = &peer2_data,
        .extensions = peer2_ext,
        .extensions_len = peer2_ext_len
    };
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &peer2_packet), XGL_OK);

    xgl_packet_data_t peer3_data = {
        .ref_count = 1,
        .data_len = sizeof(peer3_part),
        .data = peer3_part,
        .owned_data = nullptr
    };
    xgl_packet_t peer3_packet = {
        .source_id = 3,
        .target_id = 1,
        .session_id = 0,
        .connection_id = 22,
        .session_epoch = 200,
        .data_type = 1,
        .fragment = true,
        .data = &peer3_data,
        .extensions = peer3_ext,
        .extensions_len = peer3_ext_len
    };
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &peer3_packet), XGL_OK);
    ASSERT_EQ(xgl_fragment_get_reassembly_count(ctx.fragment_mgr), 2U);

    xgl_packet_data_t reset_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = nullptr,
        .owned_data = nullptr
    };
    xgl_packet_t reset_packet = {
        .source_id = 2,
        .target_id = 1,
        .session_id = 100,
        .connection_id = 11,
        .session_epoch = 100,
        .packet_type = XGL_PACKET_TYPE_CONTROL,
        .data_type = kTransportControlReset,
        .data = &reset_data
    };
    ASSERT_EQ(xgl_transport_receive(&ctx, nullptr, &reset_packet), XGL_OK);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(ctx.fragment_mgr), 1U);

    uint8_t peer3_tail_ext_value[12] = {};
    size_t peer3_tail_ext_value_len = 0;
    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(peer3_tail_ext_value,
                                                 sizeof(peer3_tail_ext_value),
                                                 20,
                                                 2,
                                                 4,
                                                 &peer3_tail_ext_value_len),
              XGL_OK);
    uint8_t peer3_tail_ext[16] = {};
    size_t peer3_tail_ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(peer3_tail_ext,
                                  sizeof(peer3_tail_ext),
                                  XGL_WIRE_EXT_FRAGMENT,
                                  peer3_tail_ext_value,
                                  peer3_tail_ext_value_len,
                                  &peer3_tail_ext_len),
              XGL_OK);

    xgl_packet_data_t peer3_tail_data = {
        .ref_count = 1,
        .data_len = sizeof(peer3_tail),
        .data = peer3_tail,
        .owned_data = nullptr
    };
    peer3_packet.extensions = peer3_tail_ext;
    peer3_packet.extensions_len = peer3_tail_ext_len;
    peer3_packet.data = &peer3_tail_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &peer3_packet), XGL_OK);
    ASSERT_EQ(rx_tracker.receive_count, 1);
    ASSERT_EQ(rx_tracker.payloads.size(), 1U);
    EXPECT_EQ(rx_tracker.payloads[0], std::vector<uint8_t>({'x', 'y', 'z', 'w'}));

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, UnreliablePacketsWithSameSequenceAreDelivered) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_NONE,
        .priority = 0,
        .data = &packet_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(rx_tracker.receive_count, 2);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, ReliableDuplicateDetectionIsScopedBySource) {
    LowerLayerSpy spy;
    xgl_layer_interface_t lower_layer = {};
    xgl_layer_interface_init(&lower_layer, &spy, spy_send, nullptr, nullptr);

    xgl_layer_stats_t stats = {};
    uint64_t tx_retries = 0;
    RxTracker rx_tracker;
    xgl_transport_ctx_t ctx;
    xgl_transport_config_t config = make_transport_config(&lower_layer, &stats, &tx_retries);
    config.rx_callback = spy_receive;
    config.callback_user_data = &rx_tracker;

    ASSERT_EQ(xgl_transport_init(&ctx, &config), XGL_OK);

    const uint8_t payload[] = {'p', 'i', 'n', 'g'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = 2,
        .target_id = 1,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 0,
        .data = &packet_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);

    packet.source_id = 3;
    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_OK);

    EXPECT_EQ(rx_tracker.receive_count, 2);
    EXPECT_EQ(spy.send_count, 3);

    xgl_transport_destroy(&ctx);
}
