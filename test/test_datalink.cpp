/**
 * \file            test_datalink.cpp
 * \brief           Data link layer unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <xgl/xgl_datalink.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_parser.h>
#include <xgl/xgl_types.h>
#include <xgl/xgl_error.h>
#include <vector>
#include <cstring>

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgPointee;
using ::testing::Invoke;

/*---------------------------------------------------------------------------*/
/* Mock Physical Layer                                                       */
/*---------------------------------------------------------------------------*/

class MockPhyOps {
public:
    MOCK_METHOD(xgl_error_t, tx, (const uint8_t* data, size_t len, void* user_data));
    MOCK_METHOD(xgl_error_t, rx, (uint8_t* buffer, size_t* len, void* user_data));
};

/* Global mock instance for C callbacks */
static MockPhyOps* g_mock_phy = nullptr;

/* C callback wrappers */
static xgl_error_t mock_phy_tx(const uint8_t* data, size_t len, void* user_data) {
    return g_mock_phy->tx(data, len, user_data);
}

static xgl_error_t mock_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    return g_mock_phy->rx(buffer, len, user_data);
}

/*---------------------------------------------------------------------------*/
/* Mock Callbacks                                                            */
/*---------------------------------------------------------------------------*/

class MockCallbacks {
public:
    MOCK_METHOD(void, rx_callback, 
                (xgl_handle_t handle, uint8_t source_id, uint8_t data_type,
                 const uint8_t* data, size_t len, void* user_data));
    MOCK_METHOD(void, error_callback,
                (xgl_handle_t handle, xgl_error_t error, 
                 const char* message, void* user_data));
};

static MockCallbacks* g_mock_callbacks = nullptr;

static void mock_rx_callback(xgl_handle_t handle, uint8_t source_id, 
                             uint8_t data_type, const uint8_t* data, 
                             size_t len, void* user_data) {
    g_mock_callbacks->rx_callback(handle, source_id, data_type, data, len, user_data);
}

static void mock_error_callback(xgl_handle_t handle, xgl_error_t error,
                                const char* message, void* user_data) {
    g_mock_callbacks->error_callback(handle, error, message, user_data);
}

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglDatalinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_mock_phy = &mock_phy;
        g_mock_callbacks = &mock_callbacks;
        
        /* Initialize PHY operations */
        phy_ops.tx = mock_phy_tx;
        phy_ops.rx = mock_phy_rx;
        phy_ops.user_data = nullptr;
        
        /* Initialize statistics */
        std::memset(&stats, 0, sizeof(stats));
        
        /* Initialize context */
        xgl_datalink_init(&ctx, rx_cache, sizeof(rx_cache), &stats, 
                         SOURCE_ID, mock_rx_callback, mock_error_callback, nullptr);
    }
    
    void TearDown() override {
        g_mock_phy = nullptr;
        g_mock_callbacks = nullptr;
    }
    
    MockPhyOps mock_phy;
    MockCallbacks mock_callbacks;
    xgl_phy_ops_t phy_ops;
    xgl_statistics_t stats;
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
    xgl_statistics_t test_stats;
    
    xgl_error_t err = xgl_datalink_init(&test_ctx, cache, sizeof(cache), 
                                       &test_stats, SOURCE_ID, 
                                       nullptr, nullptr, nullptr);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(test_ctx.source_id, SOURCE_ID);
    EXPECT_EQ(test_ctx.rx_cache, cache);
    EXPECT_EQ(test_ctx.rx_cache_size, sizeof(cache));
}

TEST_F(XglDatalinkTest, InitNullPointer) {
    uint8_t cache[256];
    xgl_statistics_t test_stats;
    
    EXPECT_EQ(xgl_datalink_init(nullptr, cache, sizeof(cache), 
                                &test_stats, SOURCE_ID, nullptr, nullptr, nullptr),
              XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Frame Transmission Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, SendFrameSuccess) {
    /* Build test frame */
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    
    xgl_error_t err = xgl_frame_build(&frame, SOURCE_ID, TARGET_ID, 0x01,
                                     0x00, 0x00, payload, sizeof(payload),
                                     true, 0);
    ASSERT_EQ(err, XGL_OK);
    
    /* Expect TX call */
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_OK));
    
    /* Send frame */
    err = xgl_datalink_send(&phy_ops, &frame, &stats, nullptr, nullptr);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_GT(stats.tx_bytes, 0);
}

TEST_F(XglDatalinkTest, SendFrameNullPointer) {
    xgl_frame_t frame;
    
    EXPECT_EQ(xgl_datalink_send(nullptr, &frame, &stats, nullptr, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_send(&phy_ops, nullptr, &stats, nullptr, nullptr),
              XGL_ERR_NULL_POINTER);
}

TEST_F(XglDatalinkTest, SendFramePhyFailure) {
    /* Build test frame */
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03};
    
    xgl_error_t err = xgl_frame_build(&frame, SOURCE_ID, TARGET_ID, 0x01,
                                     0x00, 0x00, payload, sizeof(payload),
                                     false, 0);
    ASSERT_EQ(err, XGL_OK);
    
    /* Expect TX call to fail */
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_ERR_TX_FAILED));
    
    /* Send frame */
    err = xgl_datalink_send(&phy_ops, &frame, &stats, nullptr, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_TX_FAILED);
    EXPECT_EQ(stats.tx_errors, 1);
}

TEST_F(XglDatalinkTest, SendRawFrameSuccess) {
    const uint8_t frame_data[] = {0x55, 0x01, 0x02, 0x03, 0x04};
    
    EXPECT_CALL(mock_phy, tx(_, sizeof(frame_data), _))
        .WillOnce(Return(XGL_OK));
    
    xgl_error_t err = xgl_datalink_send_raw(&phy_ops, frame_data, 
                                           sizeof(frame_data), &stats, 
                                           nullptr, nullptr);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_EQ(stats.tx_bytes, sizeof(frame_data));
}

/*---------------------------------------------------------------------------*/
/* Frame Reception Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, ReceiveCompleteFrame) {
    /* Build a complete frame */
    xgl_frame_t frame;
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    
    xgl_error_t err = xgl_frame_build(&frame, TARGET_ID, SOURCE_ID, 0x05,
                                     0x10, 0x00, payload, sizeof(payload),
                                     false, 3);
    ASSERT_EQ(err, XGL_OK);
    
    /* Serialize frame */
    uint8_t frame_buffer[128];
    size_t frame_len = 0;
    err = xgl_frame_serialize(frame_buffer, sizeof(frame_buffer), &frame, &frame_len);
    ASSERT_EQ(err, XGL_OK);
    
    /* Mock RX to return the frame data */
    EXPECT_CALL(mock_phy, rx(_, _, _))
        .WillOnce(DoAll(
            Invoke([frame_buffer, frame_len](uint8_t* buffer, size_t* len, void*) {
                std::memcpy(buffer, frame_buffer, frame_len);
                *len = frame_len;
                return XGL_OK;
            })
        ));
    
    /* Expect receive callback */
    EXPECT_CALL(mock_callbacks, rx_callback(_, TARGET_ID, 0x05, _, sizeof(payload), _))
        .WillOnce(Invoke([payload](xgl_handle_t, uint8_t, uint8_t, 
                                   const uint8_t* data, size_t len, void*) {
            EXPECT_EQ(len, sizeof(payload));
            EXPECT_EQ(std::memcmp(data, payload, len), 0);
        }));
    
    /* Receive frame */
    err = xgl_datalink_receive(&ctx, &phy_ops, 0, 1000);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.rx_packets, 1);
    EXPECT_EQ(stats.rx_bytes, frame_len);
}

TEST_F(XglDatalinkTest, ReceiveFrameWithInvalidCRC8) {
    /* Build a frame and corrupt CRC8 */
    xgl_frame_t frame;
    const uint8_t payload[] = {0x11, 0x22};
    
    xgl_error_t err = xgl_frame_build(&frame, TARGET_ID, SOURCE_ID, 0x01,
                                     0x00, 0x00, payload, sizeof(payload),
                                     false, 0);
    ASSERT_EQ(err, XGL_OK);
    
    /* Serialize frame */
    uint8_t frame_buffer[128];
    size_t frame_len = 0;
    err = xgl_frame_serialize(frame_buffer, sizeof(frame_buffer), &frame, &frame_len);
    ASSERT_EQ(err, XGL_OK);
    
    /* Corrupt CRC8 (at position 11 in header) */
    frame_buffer[11] ^= 0xFF;
    
    /* Mock RX to return corrupted frame */
    EXPECT_CALL(mock_phy, rx(_, _, _))
        .WillOnce(DoAll(
            Invoke([frame_buffer, frame_len](uint8_t* buffer, size_t* len, void*) {
                std::memcpy(buffer, frame_buffer, frame_len);
                *len = frame_len;
                return XGL_OK;
            })
        ));
    
    /* Receive frame - parser will detect CRC8 error and return parse error */
    err = xgl_datalink_receive(&ctx, &phy_ops, 0, 1000);
    
    EXPECT_EQ(err, XGL_OK);  /* Function succeeds but frame is rejected */
    /* Parser detects CRC8 error during parsing, increments rx_errors */
    EXPECT_EQ(stats.rx_errors, 1);
    /* Note: rx_crc8_errors is only incremented in process_frame, 
       but parser rejects frame before it gets there */
}

TEST_F(XglDatalinkTest, ReceiveFrameWithInvalidCRC16) {
    /* Build a frame and corrupt CRC16 */
    xgl_frame_t frame;
    const uint8_t payload[] = {0x33, 0x44, 0x55};
    
    xgl_error_t err = xgl_frame_build(&frame, TARGET_ID, SOURCE_ID, 0x02,
                                     0x00, 0x00, payload, sizeof(payload),
                                     false, 0);
    ASSERT_EQ(err, XGL_OK);
    
    /* Serialize frame */
    uint8_t frame_buffer[128];
    size_t frame_len = 0;
    err = xgl_frame_serialize(frame_buffer, sizeof(frame_buffer), &frame, &frame_len);
    ASSERT_EQ(err, XGL_OK);
    
    /* Corrupt CRC16 (last 2 bytes) */
    frame_buffer[frame_len - 1] ^= 0xFF;
    
    /* Mock RX to return corrupted frame */
    EXPECT_CALL(mock_phy, rx(_, _, _))
        .WillOnce(DoAll(
            Invoke([frame_buffer, frame_len](uint8_t* buffer, size_t* len, void*) {
                std::memcpy(buffer, frame_buffer, frame_len);
                *len = frame_len;
                return XGL_OK;
            })
        ));
    
    /* Receive frame - parser will detect CRC16 error and return parse error */
    err = xgl_datalink_receive(&ctx, &phy_ops, 0, 1000);
    
    EXPECT_EQ(err, XGL_OK);  /* Function succeeds but frame is rejected */
    /* Parser detects CRC16 error during parsing, increments rx_errors */
    EXPECT_EQ(stats.rx_errors, 1);
    /* Note: rx_crc16_errors is only incremented in process_frame, 
       but parser rejects frame before it gets there */
}

TEST_F(XglDatalinkTest, ReceiveNoData) {
    /* Mock RX to return no data */
    EXPECT_CALL(mock_phy, rx(_, _, _))
        .WillOnce(DoAll(
            SetArgPointee<1>(0),
            Return(XGL_OK)
        ));
    
    /* Receive should succeed with no data */
    xgl_error_t err = xgl_datalink_receive(&ctx, &phy_ops, 0, 1000);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.rx_packets, 0);
}

TEST_F(XglDatalinkTest, ReceiveParserTimeout) {
    /* Send partial frame data */
    const uint8_t partial_data[] = {0x55, 0x01, 0x02};  /* SOF + partial header */
    
    /* First call: send partial data */
    EXPECT_CALL(mock_phy, rx(_, _, _))
        .WillOnce(DoAll(
            Invoke([partial_data](uint8_t* buffer, size_t* len, void*) {
                std::memcpy(buffer, partial_data, sizeof(partial_data));
                *len = sizeof(partial_data);
                return XGL_OK;
            })
        ))
        .WillOnce(DoAll(
            SetArgPointee<1>(0),
            Return(XGL_OK)
        ));
    
    /* Expect error callback for timeout */
    EXPECT_CALL(mock_callbacks, error_callback(_, XGL_ERR_TIMEOUT, _, _))
        .Times(1);
    
    /* First receive - partial data */
    xgl_error_t err = xgl_datalink_receive(&ctx, &phy_ops, 0, 1000);
    EXPECT_EQ(err, XGL_OK);
    
    /* Second receive - timeout */
    err = xgl_datalink_receive(&ctx, &phy_ops, 2000, 1000);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.rx_errors, 1);
}

/*---------------------------------------------------------------------------*/
/* Statistics Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglDatalinkTest, StatisticsTracking) {
    /* Build and send a frame */
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02};
    
    xgl_error_t err = xgl_frame_build(&frame, SOURCE_ID, TARGET_ID, 0x01,
                                     0x00, 0x00, payload, sizeof(payload),
                                     false, 0);
    ASSERT_EQ(err, XGL_OK);
    
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .WillOnce(Return(XGL_OK));
    
    err = xgl_datalink_send(&phy_ops, &frame, &stats, nullptr, nullptr);
    EXPECT_EQ(err, XGL_OK);
    
    /* Verify statistics */
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_GT(stats.tx_bytes, 0);
    EXPECT_EQ(stats.tx_errors, 0);
}

