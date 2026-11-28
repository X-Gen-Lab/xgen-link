/**
 * \file            test_config.cpp
 * \brief           Configuration API unit tests
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_config.h"
#include "test_host_allocator.h"

#include <xgl/xgl.h>

#include <algorithm>
#include <gtest/gtest.h>
#include <internal/xgl_allocator.h>

static xgl_error_t mock_auth_sign(const xgl_auth_input_t* /*input*/,
                                  uint8_t* tag, size_t tag_capacity,
                                  size_t* tag_len, void* /*user_data*/) {
    if (tag == nullptr || tag_len == nullptr || tag_capacity < 4U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    tag[0] = 0xA5;
    tag[1] = 0x5A;
    tag[2] = 0xC3;
    tag[3] = 0x3C;
    *tag_len = 4U;
    return XGL_OK;
}

static xgl_error_t mock_auth_verify(const xgl_auth_input_t* /*input*/,
                                    const uint8_t* tag, size_t tag_len,
                                    bool* valid, void* /*user_data*/) {
    if (tag == nullptr || valid == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    *valid = (tag_len == 4U && tag[0] == 0xA5 && tag[1] == 0x5A &&
              tag[2] == 0xC3 && tag[3] == 0x3C);
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglConfigTest : public ::testing::Test {
  protected:
    xgl_config_t config;

    void SetUp() override {
        /* Initialize config to zero */
        memset(&config, 0, sizeof(xgl_config_t));
    }
};

/*---------------------------------------------------------------------------*/
/* Configuration Validation Tests                                            */
/*---------------------------------------------------------------------------*/

TEST_F(XglConfigTest, ValidateNullPointer) {
    EXPECT_EQ(xgl_config_validate(NULL), XGL_ERR_NULL_POINTER);
}

TEST_F(XglConfigTest, ValidateValidConfig) {
    config = xgl_test_config();
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
}

TEST_F(XglConfigTest, ValidateRejectsReservedSourceId) {
    config = xgl_test_config();
    config.source_id = 0;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRejectsReservedCodecFeatureFlags) {
    config = xgl_test_config();
    config.features.enable_compression = true;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    config = xgl_test_config();
    config.features.enable_encryption = true;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateAuthRequiredNeedsProvider) {
    config = xgl_test_config();
    config.auth_required = true;
    config.auth_provider = nullptr;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    xgl_auth_provider_t provider = {};
    provider.sign = mock_auth_sign;
    provider.verify = mock_auth_verify;
    provider.user_data = nullptr;
    config.auth_provider = &provider;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    provider.tag_len = 4U;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
}

TEST_F(XglConfigTest, ValidateRejectsAuthTagLengthAboveProtocolLimit) {
    config = xgl_test_config();
    config.auth_required = true;

    xgl_auth_provider_t provider = {};
    provider.sign = mock_auth_sign;
    provider.verify = mock_auth_verify;
    provider.tag_len = 33U;
    provider.user_data = nullptr;
    config.auth_provider = &provider;

    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateZeroTxPacketCapacity) {
    config = xgl_test_config();
    config.features.max_tx_packets = 0U;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, WorkspaceRejectsOverflowingTxCapacity) {
    config = xgl_test_config();
    config.features.max_tx_packets = SIZE_MAX;
    xgl_memory_requirements_t requirements{};
    EXPECT_EQ(xgl_memory_requirements(&config, &requirements),
              XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRxBufferSizeTooSmall) {
    config = xgl_test_config();
    config.memory.rx_buffer_size = 32; /* Below minimum of 64 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRxBufferSizeTooLarge) {
    config = xgl_test_config();
    config.memory.rx_buffer_size = 8192; /* Above maximum of 4096 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateAckTimeoutTooSmall) {
    config = xgl_test_config();
    config.protocol.ack_timeout_ms = 50; /* Below minimum of 100 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateAckTimeoutTooLarge) {
    config = xgl_test_config();
    config.protocol.ack_timeout_ms = 20000; /* Above maximum of 10000 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRetryCountTooSmall) {
    config = xgl_test_config();
    config.protocol.max_retry_count = 0; /* Below minimum of 1 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRetryCountTooLarge) {
    config = xgl_test_config();
    config.protocol.max_retry_count = 20; /* Above maximum of 10 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateWindowSizeTooSmall) {
    config = xgl_test_config();
    config.protocol.window_size = 0; /* Below minimum of 1 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateWindowSizeTooLarge) {
    config = xgl_test_config();
    config.protocol.window_size = 64; /* Above maximum of 32 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateFrameSizeTooSmall) {
    config = xgl_test_config();
    config.protocol.max_frame_size = 32; /* Below minimum of 64 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateFrameSizeTooLarge) {
    config = xgl_test_config();
    config.protocol.max_frame_size = 4096; /* Above maximum of 2048 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateFrameSizeSmallerThanHeader) {
    config = xgl_test_config();
    config.protocol.max_frame_size = 10; /* Smaller than header + CRC */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRxBufferTooSmallForFrame) {
    config = xgl_test_config();
    config.protocol.max_frame_size = 512;
    config.memory.rx_buffer_size = 256; /* Too small for full frame */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_BUFFER_TOO_SMALL);
}

TEST_F(XglConfigTest, ValidateRouteTableLengthWithoutTable) {
    config = xgl_test_config();
    config.route_table_len = 5;
    config.route_table = NULL; /* Length > 0 but table is NULL */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRouteTableWithNullPhy) {
    config = xgl_test_config();

    xgl_route_item_t routes[1];
    routes[0].target_id = 2;
    routes[0].phy = NULL; /* NULL PHY */
    routes[0].max_frame_size = 256;
    routes[0].read_freq_hz = 100;
    routes[0].metric = 1;

    config.route_table = routes;
    config.route_table_len = 1;

    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRouteTableWithNullPhyOps) {
    config = xgl_test_config();

    xgl_phy_ops_t phy;
    phy.tx = NULL; /* NULL TX function */
    phy.rx = NULL; /* NULL RX function */
    phy.user_data = NULL;

    xgl_route_item_t routes[1];
    routes[0].target_id = 2;
    routes[0].phy = &phy;
    routes[0].max_frame_size = 256;
    routes[0].read_freq_hz = 100;
    routes[0].metric = 1;

    config.route_table = routes;
    config.route_table_len = 1;

    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRouteTableWithInvalidFrameSize) {
    config = xgl_test_config();

    /* Mock PHY operations */
    auto mock_tx = [](const uint8_t* /*data*/, size_t /*len*/,
                      void* /*user_data*/) -> xgl_error_t { return XGL_OK; };
    auto mock_rx = [](uint8_t* /*buffer*/, size_t* /*len*/,
                      void* /*user_data*/) -> xgl_error_t { return XGL_OK; };

    xgl_phy_ops_t phy;
    phy.tx = mock_tx;
    phy.rx = mock_rx;
    phy.user_data = NULL;

    xgl_route_item_t routes[1];
    routes[0].target_id = 2;
    routes[0].phy = &phy;
    routes[0].max_frame_size = 32; /* Too small */
    routes[0].read_freq_hz = 100;
    routes[0].metric = 1;

    config.route_table = routes;
    config.route_table_len = 1;

    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRouteTableWithZeroFrequency) {
    config = xgl_test_config();

    /* Mock PHY operations */
    auto mock_tx = [](const uint8_t* /*data*/, size_t /*len*/,
                      void* /*user_data*/) -> xgl_error_t { return XGL_OK; };
    auto mock_rx = [](uint8_t* /*buffer*/, size_t* /*len*/,
                      void* /*user_data*/) -> xgl_error_t { return XGL_OK; };

    xgl_phy_ops_t phy;
    phy.tx = mock_tx;
    phy.rx = mock_rx;
    phy.user_data = NULL;

    xgl_route_item_t routes[1];
    routes[0].target_id = 2;
    routes[0].phy = &phy;
    routes[0].max_frame_size = 256;
    routes[0].read_freq_hz = 0; /* Zero frequency */
    routes[0].metric = 1;

    config.route_table = routes;
    config.route_table_len = 1;

    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateValidRouteTable) {
    config = xgl_test_config();

    /* Mock PHY operations */
    auto mock_tx = [](const uint8_t* /*data*/, size_t /*len*/,
                      void* /*user_data*/) -> xgl_error_t { return XGL_OK; };
    auto mock_rx = [](uint8_t* /*buffer*/, size_t* /*len*/,
                      void* /*user_data*/) -> xgl_error_t { return XGL_OK; };

    xgl_phy_ops_t phy;
    phy.tx = mock_tx;
    phy.rx = mock_rx;
    phy.user_data = NULL;

    xgl_route_item_t routes[1];
    routes[0].target_id = 2;
    routes[0].phy = &phy;
    routes[0].max_frame_size = 256;
    routes[0].read_freq_hz = 100;
    routes[0].metric = 1;

    config.route_table = routes;
    config.route_table_len = 1;

    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
}

/*---------------------------------------------------------------------------*/
/* Integration with Instance Creation                                        */
/*---------------------------------------------------------------------------*/

TEST_F(XglConfigTest, CreateInstanceWithValidConfig) {
    config = xgl_test_config();

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    EXPECT_NE(handle, nullptr);

    xgl_destroy(handle);
}

TEST_F(XglConfigTest, CreateInstanceWithInvalidConfig) {
    config = xgl_test_config();
    config.features.max_tx_packets = 0U;

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    EXPECT_EQ(handle, nullptr);
}
