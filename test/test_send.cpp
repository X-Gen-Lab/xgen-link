/**
 * \file            test_send.cpp
 * \brief           Unit tests for send API
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <xgl/xgl.h>
#include <xgl/xgl_wire.h>
#include <cstring>
#include <vector>

/*---------------------------------------------------------------------------*/
/* Mock Physical Layer                                                       */
/*---------------------------------------------------------------------------*/

class MockPhy {
public:
    MOCK_METHOD(xgl_error_t, tx, (const uint8_t* data, size_t len, void* user_data));
    MOCK_METHOD(xgl_error_t, rx, (uint8_t* buffer, size_t* len, void* user_data));
};

/* Global mock instance for C callbacks */
static MockPhy* g_mock_phy = nullptr;

/* C callback wrappers */
static xgl_error_t mock_phy_tx(const uint8_t* data, size_t len, void* user_data) {
    if (g_mock_phy) {
        return g_mock_phy->tx(data, len, user_data);
    }
    return XGL_OK;
}

static xgl_error_t mock_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    if (g_mock_phy) {
        return g_mock_phy->rx(buffer, len, user_data);
    }
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglSendTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Create mock PHY */
        mock_phy = new MockPhy();
        g_mock_phy = mock_phy;
        
        /* Setup PHY operations */
        phy_ops.tx = mock_phy_tx;
        phy_ops.rx = mock_phy_rx;
        phy_ops.user_data = nullptr;
        
        /* Setup route */
        route.target_id = 2;
        route.phy = &phy_ops;
        route.max_frame_size = 256;
        route.read_freq_hz = 100;
        route.metric = 100;
        
        /* Get default configuration */
        xgl_config_get_default(&config);
        config.source_id = 1;
        config.route_table = &route;
        config.route_table_len = 1;
        
        /* Create and initialize instance */
        handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr);
        
        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK);
    }
    
    void TearDown() override {
        if (handle) {
            xgl_destroy(handle);
            handle = nullptr;
        }
        
        g_mock_phy = nullptr;
        delete mock_phy;
        mock_phy = nullptr;
    }
    
    xgl_config_t config;
    xgl_handle_t handle = nullptr;
    xgl_phy_ops_t phy_ops;
    xgl_route_item_t route;
    MockPhy* mock_phy = nullptr;
};

/*---------------------------------------------------------------------------*/
/* Parameter Validation Tests                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test send with NULL handle
 */
TEST_F(XglSendTest, SendWithNullHandle) {
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = (const uint8_t*)"test",
        .data_len = 4,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };
    
    xgl_error_t err = xgl_send(nullptr, &tx_data);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test send with NULL tx_data
 */
TEST_F(XglSendTest, SendWithNullTxData) {
    xgl_error_t err = xgl_send(handle, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test send with NULL data pointer
 */
TEST_F(XglSendTest, SendWithNullDataPointer) {
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = nullptr,
        .data_len = 4,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };
    
    xgl_error_t err = xgl_send(handle, &tx_data);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test send with zero data length
 */
TEST_F(XglSendTest, SendWithZeroDataLength) {
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = (const uint8_t*)"test",
        .data_len = 0,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };
    
    xgl_error_t err = xgl_send(handle, &tx_data);
    
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

/**
 * \brief           Test send with invalid priority
 */
TEST_F(XglSendTest, SendWithInvalidPriority) {
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = (const uint8_t*)"test",
        .data_len = 4,
        .reliable = false,
        .priority = 8,  /* Invalid: must be 0-7 */
        .timeout_ms = 0
    };
    
    xgl_error_t err = xgl_send(handle, &tx_data);
    
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

/*---------------------------------------------------------------------------*/
/* Basic Send Tests                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test basic send with valid parameters
 */
TEST_F(XglSendTest, BasicSendSuccess) {
    const uint8_t test_data[] = "Hello, World!";
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = test_data,
        .data_len = sizeof(test_data) - 1,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };
    
    /* Expect PHY TX to be called */
    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_))
        .Times(1)
        .WillOnce(testing::Return(XGL_OK));
    
    xgl_error_t err = xgl_send(handle, &tx_data);
    
    EXPECT_EQ(err, XGL_OK);
}

TEST_F(XglSendTest, SendFragmentsByRouteMaxFrameSize) {
    config.features.enable_fragmentation = true;
    ASSERT_LT(route.max_frame_size, config.protocol.max_frame_size);

    std::vector<uint8_t> data(300, 0x5A);
    xgl_tx_data_t tx_data = {
        .target_id = 2,
        .data_type = 1,
        .data = data.data(),
        .data_len = data.size(),
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };

    std::vector<size_t> frame_lengths;
    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_))
        .Times(testing::AtLeast(2))
        .WillRepeatedly(testing::Invoke([&](const uint8_t* /*bytes*/, size_t len, void* /*user_data*/) {
            frame_lengths.push_back(len);
            return XGL_OK;
        }));

    EXPECT_EQ(xgl_send(handle, &tx_data), XGL_OK);
    ASSERT_GT(frame_lengths.size(), 1U);
    for (size_t len : frame_lengths) {
        EXPECT_LE(len, route.max_frame_size);
    }
}

/**
 * \brief           Test send to non-existent route
 */
TEST_F(XglSendTest, SendToNonExistentRoute) {
    const uint8_t test_data[] = "test";
    xgl_tx_data_t tx_data = {
        .target_id = 99,  /* No route for this ID */
        .data_type = 1,
        .data = test_data,
        .data_len = sizeof(test_data) - 1,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };
    
    xgl_error_t err = xgl_send(handle, &tx_data);
    
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
}

/*---------------------------------------------------------------------------*/
/* Zero-Copy Send Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test zero-copy send with NULL handle
 */
TEST_F(XglSendTest, ZeroCopySendWithNullHandle) {
    uint8_t buffer[128];
    xgl_tx_data_zerocopy_t tx_data = {
        .buffer = buffer,
        .buffer_size = sizeof(buffer),
        .data_offset = XGL_FRAME_HEADER_SIZE,
        .data_len = 10,
        .target_id = 2,
        .data_type = 1,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };
    
    xgl_error_t err = xgl_send_zerocopy(nullptr, &tx_data);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test zero-copy send with NULL tx_data
 */
TEST_F(XglSendTest, ZeroCopySendWithNullTxData) {
    xgl_error_t err = xgl_send_zerocopy(handle, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test zero-copy send with invalid data offset
 */
TEST_F(XglSendTest, ZeroCopySendWithInvalidDataOffset) {
    uint8_t buffer[128];
    xgl_tx_data_zerocopy_t tx_data = {
        .buffer = buffer,
        .buffer_size = sizeof(buffer),
        .data_offset = 10,  /* Invalid: must be XGL_FRAME_HEADER_SIZE */
        .data_len = 10,
        .target_id = 2,
        .data_type = 1,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };
    
    xgl_error_t err = xgl_send_zerocopy(handle, &tx_data);
    
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

/**
 * \brief           Test zero-copy send with buffer too small
 */
TEST_F(XglSendTest, ZeroCopySendWithBufferTooSmall) {
    uint8_t buffer[20];
    xgl_tx_data_zerocopy_t tx_data = {
        .buffer = buffer,
        .buffer_size = sizeof(buffer),
        .data_offset = XGL_FRAME_HEADER_SIZE,
        .data_len = 100,  /* Too large for buffer */
        .target_id = 2,
        .data_type = 1,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };
    
    xgl_error_t err = xgl_send_zerocopy(handle, &tx_data);
    
    EXPECT_EQ(err, XGL_ERR_BUFFER_TOO_SMALL);
}

TEST_F(XglSendTest, ZeroCopyUnreliableUsesCallerFrameBuffer) {
    uint8_t buffer[64] = {};
    const char payload[] = "zcopy";
    memcpy(buffer + XGL_FRAME_HEADER_SIZE, payload, sizeof(payload) - 1U);

    xgl_tx_data_zerocopy_t tx_data = {
        .buffer = buffer,
        .buffer_size = sizeof(buffer),
        .data_offset = XGL_FRAME_HEADER_SIZE,
        .data_len = sizeof(payload) - 1U,
        .target_id = 2,
        .data_type = 1,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };

    EXPECT_CALL(*mock_phy, tx(buffer,
                              XGL_FRAME_HEADER_SIZE + tx_data.data_len + XGL_CRC16_SIZE,
                              testing::_))
        .Times(1)
        .WillOnce(testing::Return(XGL_OK));

    EXPECT_EQ(xgl_send_zerocopy(handle, &tx_data), XGL_OK);
    EXPECT_EQ(buffer[0], XGL_WIRE_MAGIC_0);
    EXPECT_EQ(buffer[1], XGL_WIRE_MAGIC_1);
    EXPECT_EQ(std::memcmp(buffer + XGL_FRAME_HEADER_SIZE, payload, sizeof(payload) - 1U), 0);
}

TEST_F(XglSendTest, ZeroCopyReliableIsRejectedInsteadOfImplicitCopyFallback) {
    uint8_t buffer[64] = {};
    const char payload[] = "reliable";
    memcpy(buffer + XGL_FRAME_HEADER_SIZE, payload, sizeof(payload) - 1U);

    xgl_tx_data_zerocopy_t tx_data = {
        .buffer = buffer,
        .buffer_size = sizeof(buffer),
        .data_offset = XGL_FRAME_HEADER_SIZE,
        .data_len = sizeof(payload) - 1U,
        .target_id = 2,
        .data_type = 1,
        .reliable = true,
        .priority = 0,
        .timeout_ms = 0
    };

    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_EQ(xgl_send_zerocopy(handle, &tx_data), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglSendTest, ZeroCopyUnreliableRejectsPayloadExceedingRouteMtu) {
    xgl_destroy(handle);
    handle = nullptr;
    route.max_frame_size = 64;
    handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(xgl_init(handle), XGL_OK);

    uint8_t buffer[128] = {};
    memset(buffer + XGL_FRAME_HEADER_SIZE, 0xAB, 60);
    xgl_tx_data_zerocopy_t tx_data = {
        .buffer = buffer,
        .buffer_size = sizeof(buffer),
        .data_offset = XGL_FRAME_HEADER_SIZE,
        .data_len = 60,
        .target_id = 2,
        .data_type = 1,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };

    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_)).Times(0);
    EXPECT_EQ(xgl_send_zerocopy(handle, &tx_data), XGL_ERR_BUFFER_TOO_SMALL);
}

TEST_F(XglSendTest, ZeroCopyUnreliableWritesDefaultTtlAndEmptySession) {
    uint8_t buffer[64] = {};
    const char payload[] = "zcopy";
    memcpy(buffer + XGL_FRAME_HEADER_SIZE, payload, sizeof(payload) - 1U);

    xgl_tx_data_zerocopy_t tx_data = {
        .buffer = buffer,
        .buffer_size = sizeof(buffer),
        .data_offset = XGL_FRAME_HEADER_SIZE,
        .data_len = sizeof(payload) - 1U,
        .target_id = 2,
        .data_type = 1,
        .reliable = false,
        .priority = 0,
        .timeout_ms = 0
    };

    EXPECT_CALL(*mock_phy, tx(testing::_, testing::_, testing::_))
        .Times(1)
        .WillOnce(testing::Return(XGL_OK));

    ASSERT_EQ(xgl_send_zerocopy(handle, &tx_data), XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, buffer, sizeof(buffer)), XGL_OK);
    EXPECT_EQ(header.ttl, XGL_DEFAULT_TTL);
    EXPECT_EQ(header.connection_id, 0U);
}
