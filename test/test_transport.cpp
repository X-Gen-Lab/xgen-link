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

namespace {

struct LowerLayerSpy {
    int send_count = 0;
    xgl_packet_t last_packet = {};
};

struct RxTracker {
    int receive_count = 0;
};

static xgl_error_t spy_send(void* ctx, xgl_handle_t handle, void* data) {
    (void)handle;

    auto* spy = static_cast<LowerLayerSpy*>(ctx);
    auto* packet = static_cast<xgl_packet_t*>(data);
    if (spy == nullptr || packet == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }

    spy->send_count++;
    spy->last_packet = *packet;
    return XGL_OK;
}

static void spy_receive(xgl_handle_t handle,
                        uint8_t source_id,
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
    EXPECT_EQ(spy.send_count, 1);
    EXPECT_EQ(xgl_reliable_get_count(&ctx.reliable_queue), 1);
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
    EXPECT_EQ(xgl_reliable_get_count(&ctx.reliable_queue), 0);
    EXPECT_TRUE(xgl_transport_can_send(&ctx));

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
    ASSERT_EQ(spy.send_count, 1);

    xgl_reliable_packet_t* queued = xgl_reliable_find_packet(&ctx.reliable_queue, 0, 2);
    ASSERT_NE(queued, nullptr);
    queued->send_timestamp = 100;

    EXPECT_EQ(xgl_transport_run(&ctx, nullptr, 201), XGL_OK);
    EXPECT_EQ(spy.send_count, 2);
    EXPECT_EQ(tx_retries, 1);

    queued = xgl_reliable_find_packet(&ctx.reliable_queue, 0, 2);
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
