/**
 * \file            test_transport.cpp
 * \brief           Transport layer unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/xgl_transport.h>
#include <xgl/xgl_reliable.h>
#include <cstring>
#include <vector>

namespace {

struct LowerLayerSpy {
    int send_count = 0;
    xgl_packet_t last_packet = {};
    std::vector<xgl_packet_t> sent_packets;
};

struct RxTracker {
    int receive_count = 0;
};

constexpr uint8_t kTransportControlHello = 0x0E;
constexpr uint8_t kTransportControlReset = 0x0F;
constexpr uint8_t kTransportControlNack = 0x0D;

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
    return XGL_OK;
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
    (void)data;
    (void)len;

    auto* tracker = static_cast<RxTracker*>(user_data);
    if (tracker != nullptr) {
        tracker->receive_count++;
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

    xgl_packet_data_t ack_data = {
        .ref_count = 1,
        .data_len = 0,
        .data = nullptr,
        .owned_data = nullptr
    };
    xgl_packet_t ack_packet = {
        .source_id = 2,
        .target_id = 1,
        .seq_num = 0,
        .ack_num = 0,
        .data_type = 0,
        .reliable = XGL_ATTR_RELIABLE_ACK,
        .priority = 7,
        .data = &ack_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &ack_packet), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&find_peer(&ctx, 2)->reliable_queue), 0);
    EXPECT_TRUE(xgl_transport_can_send(&ctx));

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
        .seq_num = 0,
        .ack_num = 0,
        .data_type = 0,
        .reliable = XGL_ATTR_RELIABLE_ACK,
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
        .seq_num = 0,
        .ack_num = 0,
        .session_id = wrong_session,
        .data_type = 0,
        .reliable = XGL_ATTR_RELIABLE_ACK,
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
        .seq_num = 0,
        .ack_num = 0,
        .session_id = 9,
        .data_type = kTransportControlHello,
        .reliable = XGL_ATTR_RELIABLE_NONE,
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
        .seq_num = 0,
        .ack_num = 0,
        .session_id = 11,
        .data_type = kTransportControlReset,
        .reliable = XGL_ATTR_RELIABLE_NONE,
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
        .seq_num = 0,
        .ack_num = 0,
        .session_id = 5,
        .data_type = kTransportControlHello,
        .reliable = XGL_ATTR_RELIABLE_NONE,
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
        .seq_num = 7,
        .ack_num = 0,
        .session_id = 4,
        .data_type = 1,
        .reliable = XGL_ATTR_RELIABLE_TX,
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
        .seq_num = 0,
        .ack_num = 0,
        .session_id = 13,
        .data_type = 1,
        .reliable = XGL_ATTR_RELIABLE_TX,
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

    uint8_t first_fragment[] = {
        3, 0, 2, 0, 0,
        'a', 'b', 'c', 'd'
    };
    xgl_packet_data_t first_data = {
        .ref_count = 1,
        .data_len = sizeof(first_fragment),
        .data = first_fragment,
        .owned_data = nullptr
    };
    xgl_packet_t first_packet = {
        .source_id = 2,
        .target_id = 1,
        .seq_num = 0,
        .ack_num = 0,
        .session_id = 5,
        .data_type = 1,
        .reliable = XGL_ATTR_RELIABLE_NONE,
        .fragment = true,
        .priority = 0,
        .data = &first_data
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
        .seq_num = 0,
        .ack_num = 0,
        .session_id = 6,
        .data_type = kTransportControlReset,
        .reliable = XGL_ATTR_RELIABLE_NONE,
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
    stale_packet.seq_num = 1;
    stale_packet.data = &second_data;

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &stale_packet), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(rx_tracker.receive_count, 0);

    xgl_transport_destroy(&ctx);
}

TEST(XglTransportTest, OutOfOrderReliablePacketSendsNackForExpectedSequence) {
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
        .seq_num = 2,
        .ack_num = 0,
        .session_id = 5,
        .data_type = 1,
        .reliable = XGL_ATTR_RELIABLE_TX,
        .priority = 0,
        .data = &packet_data
    };

    EXPECT_EQ(xgl_transport_receive(&ctx, nullptr, &packet), XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(rx_tracker.receive_count, 0);
    ASSERT_EQ(spy.send_count, 1);
    EXPECT_EQ(spy.last_packet.data_type, kTransportControlNack);
    EXPECT_EQ(spy.last_packet.reliable, XGL_ATTR_RELIABLE_ACK);
    EXPECT_EQ(spy.last_packet.ack_num, 0U);
    EXPECT_EQ(spy.last_packet.session_id, 5U);

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
        xgl_reliable_find_packet(&find_peer(&ctx, 2)->reliable_queue, 0, 2);
    ASSERT_NE(queued, nullptr);
    queued->send_timestamp = 100;

    EXPECT_EQ(xgl_transport_run(&ctx, nullptr, 201), XGL_OK);
    EXPECT_EQ(spy.send_count, 3);
    EXPECT_EQ(tx_retries, 1);

    queued = xgl_reliable_find_packet(&find_peer(&ctx, 2)->reliable_queue, 0, 2);
    ASSERT_NE(queued, nullptr);
    EXPECT_EQ(queued->retry_count, 1);
    EXPECT_EQ(queued->send_timestamp, 201U);
    EXPECT_EQ(spy.last_packet.target_id, 2);
    EXPECT_EQ(spy.last_packet.seq_num, 0);
    EXPECT_EQ(spy.last_packet.reliable, 1);

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
        .seq_num = 0,
        .ack_num = 0,
        .data_type = 1,
        .reliable = XGL_ATTR_RELIABLE_NONE,
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
        .seq_num = 0,
        .ack_num = 0,
        .data_type = 1,
        .reliable = XGL_ATTR_RELIABLE_TX,
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
