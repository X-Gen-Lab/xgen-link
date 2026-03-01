/**
 * \file            test_datalink.cpp
 * \brief           Unit tests for data link layer
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <xgl/xgl_datalink.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_network.h>
#include <xgl/xgl_transport.h>
#include <xgl/xgl_route.h>
#include <cstring>

using ::testing::_;
using ::testing::Return;

/*---------------------------------------------------------------------------*/
/* Mock PHY Operations                                                       */
/*---------------------------------------------------------------------------*/

class MockPhyOps {
public:
    MOCK_METHOD(xgl_error_t, tx, (const uint8_t* data, size_t len, void* user_data));
    MOCK_METHOD(xgl_error_t, rx, (uint8_t* buffer, size_t* len, void* user_data));
};

static MockPhyOps* g_mock_phy = nullptr;

static xgl_error_t mock_phy_tx(const uint8_t* data, size_t len, void* user_data) {
    return g_mock_phy->tx(data, len, user_data);
}

static xgl_error_t mock_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    return g_mock_phy->rx(buffer, len, user_data);
}

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglDatalinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_mock_phy = &mock_phy;
        
        /* Initialize PHY operations */
        phy_ops.tx = mock_phy_tx;
        phy_ops.rx = mock_phy_rx;
        phy_ops.user_data = nullptr;
        
        /* Initialize statistics */
        std::memset(&stats, 0, sizeof(stats));
        rx_crc8_errors = 0;
        rx_crc16_errors = 0;
        
        /* Initialize datalink context */
        xgl_datalink_config_t config = {
            .rx_cache = rx_cache,
            .rx_cache_size = sizeof(rx_cache),
            .source_id = SOURCE_ID,
            .stats = &stats,
            .rx_crc8_errors = &rx_crc8_errors,
            .rx_crc16_errors = &rx_crc16_errors,
            .upper_layer = nullptr,
            .error_callback = nullptr,
            .callback_user_data = nullptr
        };
        xgl_datalink_init(&ctx, &config);
    }
    
    void TearDown() override {
        g_mock_phy = nullptr;
    }
    
    MockPhyOps mock_phy;
    xgl_phy_ops_t phy_ops;
    xgl_layer_stats_t stats;
    uint64_t rx_crc8_errors;
    uint64_t rx_crc16_errors;
    xgl_datalink_ctx_t ctx;
    uint8_t rx_cache[512];
    
    static constexpr uint8_t SOURCE_ID = 0x01;
    static constexpr uint8_t TARGET_ID = 0x02;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, InitSuccess) {
    xgl_datalink_ctx_t test_ctx;
    uint8_t cache[256];
    xgl_layer_stats_t test_stats = {0};
    uint64_t crc8 = 0, crc16 = 0;
    
    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &test_stats,
        .rx_crc8_errors = &crc8,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr
    };
    xgl_error_t err = xgl_datalink_init(&test_ctx, &config);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(test_ctx.source_id, SOURCE_ID);
}

TEST_F(XglDatalinkTest, InitNullPointer) {
    uint8_t cache[256];
    xgl_layer_stats_t test_stats = {0};
    uint64_t crc8 = 0, crc16 = 0;
    
    xgl_datalink_config_t config = {
        .rx_cache = cache,
        .rx_cache_size = sizeof(cache),
        .source_id = SOURCE_ID,
        .stats = &test_stats,
        .rx_crc8_errors = &crc8,
        .rx_crc16_errors = &crc16,
        .upper_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr
    };
    
    EXPECT_EQ(xgl_datalink_init(nullptr, &config), XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Frame Transmission Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, SendFrameSuccess) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = 0x01,
        .seq_num = 0x00,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };
    
    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);
    
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_OK));
    
    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_GT(stats.tx_bytes, 0);
}

TEST_F(XglDatalinkTest, SendFrameNullPointer) {
    xgl_frame_t frame;
    
    EXPECT_EQ(xgl_datalink_send(nullptr, &phy_ops, &frame),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_send(&ctx, nullptr, &frame),
              XGL_ERR_NULL_POINTER);
}

TEST_F(XglDatalinkTest, SendFramePhyError) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0xAA};
    
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = 0x01,
        .seq_num = 0x00,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };
    
    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);
    
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_ERR_TX_FAILED));
    
    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_ERR_TX_FAILED);
    EXPECT_EQ(stats.tx_errors, 1);
}

/*---------------------------------------------------------------------------*/
/* Statistics Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, StatisticsTracking) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02};
    
    xgl_frame_params_t params = {
        .source_id = SOURCE_ID,
        .target_id = TARGET_ID,
        .data_type = 0x01,
        .seq_num = 0x00,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };
    
    xgl_error_t err = xgl_frame_build(&frame, &params);
    ASSERT_EQ(err, XGL_OK);
    
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_OK));
    
    err = xgl_datalink_send(&ctx, &phy_ops, &frame);
    EXPECT_EQ(err, XGL_OK);
    
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_GT(stats.tx_bytes, 0);
    EXPECT_EQ(stats.tx_errors, 0);
}
