/**
 * \file            test_config.cpp
 * \brief           Configuration API unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/xgl_allocator.h>

static xgl_error_t mock_auth_sign(uint32_t /*key_id*/,
                                  const uint8_t* /*aad*/,
                                  size_t /*aad_len*/,
                                  const uint8_t* /*payload*/,
                                  size_t /*payload_len*/,
                                  uint8_t* tag,
                                  size_t tag_capacity,
                                  size_t* tag_len,
                                  void* /*user_data*/) {
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

static xgl_error_t mock_auth_verify(uint32_t /*key_id*/,
                                    const uint8_t* /*aad*/,
                                    size_t /*aad_len*/,
                                    const uint8_t* /*payload*/,
                                    size_t /*payload_len*/,
                                    const uint8_t* tag,
                                    size_t tag_len,
                                    bool* valid,
                                    void* /*user_data*/) {
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
/* Default Configuration Tests                                               */
/*---------------------------------------------------------------------------*/

TEST_F(XglConfigTest, GetDefaultConfig) {
    /* Get default configuration */
    xgl_config_get_default(&config);
    
    /* Verify default values (medium preset) */
    EXPECT_STREQ(config.name, "medium");
    EXPECT_EQ(config.source_id, 1);
    EXPECT_EQ(config.memory.tx_pool_size, 4096);
    EXPECT_EQ(config.memory.rx_buffer_size, 544);
    EXPECT_EQ(config.protocol.ack_timeout_ms, 1000);
    EXPECT_EQ(config.protocol.max_retry_count, 5);
    EXPECT_EQ(config.protocol.window_size, 8);
    EXPECT_EQ(config.protocol.max_frame_size, 512);
    EXPECT_TRUE(config.features.enable_fragmentation);
    EXPECT_FALSE(config.features.enable_compression);
    EXPECT_FALSE(config.features.enable_encryption);
    EXPECT_FALSE(config.features.thread_safe);
}

TEST_F(XglConfigTest, GetDefaultConfigNullPointer) {
    /* Should not crash with NULL pointer */
    xgl_config_get_default(NULL);
}

/*---------------------------------------------------------------------------*/
/* Preset Configuration Tests                                                */
/*---------------------------------------------------------------------------*/

TEST_F(XglConfigTest, GetPresetTiny) {
    xgl_config_get_preset_tiny(&config);
    
    EXPECT_STREQ(config.name, "tiny");
    EXPECT_EQ(config.memory.tx_pool_size, 1024);
    EXPECT_EQ(config.memory.rx_buffer_size, 160);
    EXPECT_EQ(config.protocol.max_retry_count, 3);
    EXPECT_EQ(config.protocol.window_size, 2);
    EXPECT_EQ(config.protocol.max_frame_size, 128);
    EXPECT_FALSE(config.features.enable_fragmentation);
    EXPECT_FALSE(config.features.enable_compression);
    EXPECT_FALSE(config.features.enable_encryption);
}

TEST_F(XglConfigTest, GetPresetSmall) {
    xgl_config_get_preset_small(&config);
    
    EXPECT_STREQ(config.name, "small");
    EXPECT_EQ(config.memory.tx_pool_size, 2048);
    EXPECT_EQ(config.memory.rx_buffer_size, 288);
    EXPECT_EQ(config.protocol.max_retry_count, 5);
    EXPECT_EQ(config.protocol.window_size, 4);
    EXPECT_EQ(config.protocol.max_frame_size, 256);
    EXPECT_TRUE(config.features.enable_fragmentation);
    EXPECT_FALSE(config.features.enable_compression);
    EXPECT_FALSE(config.features.enable_encryption);
}

TEST_F(XglConfigTest, GetPresetMedium) {
    xgl_config_get_preset_medium(&config);
    
    EXPECT_STREQ(config.name, "medium");
    EXPECT_EQ(config.memory.tx_pool_size, 4096);
    EXPECT_EQ(config.memory.rx_buffer_size, 544);
    EXPECT_EQ(config.protocol.max_retry_count, 5);
    EXPECT_EQ(config.protocol.window_size, 8);
    EXPECT_EQ(config.protocol.max_frame_size, 512);
    EXPECT_TRUE(config.features.enable_fragmentation);
    EXPECT_FALSE(config.features.enable_compression);
    EXPECT_FALSE(config.features.enable_encryption);
}

TEST_F(XglConfigTest, GetPresetLarge) {
    xgl_config_get_preset_large(&config);
    
    EXPECT_STREQ(config.name, "large");
    EXPECT_EQ(config.memory.tx_pool_size, 8192);
    EXPECT_EQ(config.memory.rx_buffer_size, 1056);
    EXPECT_EQ(config.protocol.max_retry_count, 7);
    EXPECT_EQ(config.protocol.window_size, 16);
    EXPECT_EQ(config.protocol.max_frame_size, 1024);
    EXPECT_TRUE(config.features.enable_fragmentation);
    EXPECT_FALSE(config.features.enable_compression);
    EXPECT_FALSE(config.features.enable_encryption);
}

TEST_F(XglConfigTest, GetPresetProductionRequiresAuthentication) {
    xgl_config_get_preset_production(&config);

    EXPECT_STREQ(config.name, "production");
    EXPECT_TRUE(config.auth_required);
    EXPECT_NE(config.auth_key_id, 0U);
    EXPECT_EQ(config.auth_provider, nullptr);
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    xgl_auth_provider_t provider = {
        .sign = mock_auth_sign,
        .verify = mock_auth_verify,
        .user_data = nullptr
    };
    config.auth_provider = &provider;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    config.memory.allocator = xgl_allocator_get_default();
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
}

/*---------------------------------------------------------------------------*/
/* Configuration Validation Tests                                            */
/*---------------------------------------------------------------------------*/

TEST_F(XglConfigTest, ValidateNullPointer) {
    EXPECT_EQ(xgl_config_validate(NULL), XGL_ERR_NULL_POINTER);
}

TEST_F(XglConfigTest, ValidateValidConfig) {
    xgl_config_get_default(&config);
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
}

TEST_F(XglConfigTest, ValidateRejectsReservedSourceId) {
    xgl_config_get_default(&config);
    config.source_id = 0;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRejectsReservedCodecFeatureFlags) {
    xgl_config_get_default(&config);
    config.features.enable_compression = true;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    xgl_config_get_default(&config);
    config.features.enable_encryption = true;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateAuthRequiredNeedsProvider) {
    xgl_config_get_default(&config);
    config.auth_required = true;
    config.auth_key_id = 7;
    config.auth_provider = nullptr;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    xgl_auth_provider_t provider = {
        .sign = mock_auth_sign,
        .verify = mock_auth_verify,
        .user_data = nullptr
    };
    config.auth_provider = &provider;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);

    config.memory.allocator = xgl_allocator_get_default();
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
}

#ifndef XGL_THREAD_SAFE
TEST_F(XglConfigTest, ValidateRejectsRuntimeThreadSafeWithoutBuildSupport) {
    xgl_config_get_default(&config);
    config.features.thread_safe = true;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}
#endif

TEST_F(XglConfigTest, ValidateTxPoolSizeTooSmall) {
    xgl_config_get_default(&config);
    config.memory.tx_pool_size = 256;  /* Below minimum of 512 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateTxPoolSizeTooLarge) {
    xgl_config_get_default(&config);
    config.memory.tx_pool_size = 100000;  /* Above maximum of 65536 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRxBufferSizeTooSmall) {
    xgl_config_get_default(&config);
    config.memory.rx_buffer_size = 32;  /* Below minimum of 64 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRxBufferSizeTooLarge) {
    xgl_config_get_default(&config);
    config.memory.rx_buffer_size = 8192;  /* Above maximum of 4096 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateAckTimeoutTooSmall) {
    xgl_config_get_default(&config);
    config.protocol.ack_timeout_ms = 50;  /* Below minimum of 100 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateAckTimeoutTooLarge) {
    xgl_config_get_default(&config);
    config.protocol.ack_timeout_ms = 20000;  /* Above maximum of 10000 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRetryCountTooSmall) {
    xgl_config_get_default(&config);
    config.protocol.max_retry_count = 0;  /* Below minimum of 1 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRetryCountTooLarge) {
    xgl_config_get_default(&config);
    config.protocol.max_retry_count = 20;  /* Above maximum of 10 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateWindowSizeTooSmall) {
    xgl_config_get_default(&config);
    config.protocol.window_size = 0;  /* Below minimum of 1 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateWindowSizeTooLarge) {
    xgl_config_get_default(&config);
    config.protocol.window_size = 64;  /* Above maximum of 32 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateFrameSizeTooSmall) {
    xgl_config_get_default(&config);
    config.protocol.max_frame_size = 32;  /* Below minimum of 64 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateFrameSizeTooLarge) {
    xgl_config_get_default(&config);
    config.protocol.max_frame_size = 4096;  /* Above maximum of 2048 */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateFrameSizeSmallerThanHeader) {
    xgl_config_get_default(&config);
    config.protocol.max_frame_size = 10;  /* Smaller than header + CRC */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRxBufferTooSmallForFrame) {
    xgl_config_get_default(&config);
    config.protocol.max_frame_size = 512;
    config.memory.rx_buffer_size = 256;  /* Too small for full frame */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_BUFFER_TOO_SMALL);
}

TEST_F(XglConfigTest, ValidateRouteTableLengthWithoutTable) {
    xgl_config_get_default(&config);
    config.route_table_len = 5;
    config.route_table = NULL;  /* Length > 0 but table is NULL */
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRouteTableWithNullPhy) {
    xgl_config_get_default(&config);
    
    xgl_route_item_t routes[1];
    routes[0].target_id = 1;
    routes[0].phy = NULL;  /* NULL PHY */
    routes[0].max_frame_size = 256;
    routes[0].read_freq_hz = 100;
    routes[0].metric = 1;
    
    config.route_table = routes;
    config.route_table_len = 1;
    
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRouteTableWithNullPhyOps) {
    xgl_config_get_default(&config);
    
    xgl_phy_ops_t phy;
    phy.tx = NULL;  /* NULL TX function */
    phy.rx = NULL;  /* NULL RX function */
    phy.user_data = NULL;
    
    xgl_route_item_t routes[1];
    routes[0].target_id = 1;
    routes[0].phy = &phy;
    routes[0].max_frame_size = 256;
    routes[0].read_freq_hz = 100;
    routes[0].metric = 1;
    
    config.route_table = routes;
    config.route_table_len = 1;
    
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRouteTableWithInvalidFrameSize) {
    xgl_config_get_default(&config);
    
    /* Mock PHY operations */
    auto mock_tx = [](const uint8_t* /*data*/, size_t /*len*/, void* /*user_data*/) -> xgl_error_t {
        return XGL_OK;
    };
    auto mock_rx = [](uint8_t* /*buffer*/, size_t* /*len*/, void* /*user_data*/) -> xgl_error_t {
        return XGL_OK;
    };
    
    xgl_phy_ops_t phy;
    phy.tx = mock_tx;
    phy.rx = mock_rx;
    phy.user_data = NULL;
    
    xgl_route_item_t routes[1];
    routes[0].target_id = 1;
    routes[0].phy = &phy;
    routes[0].max_frame_size = 32;  /* Too small */
    routes[0].read_freq_hz = 100;
    routes[0].metric = 1;
    
    config.route_table = routes;
    config.route_table_len = 1;
    
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateRouteTableWithZeroFrequency) {
    xgl_config_get_default(&config);
    
    /* Mock PHY operations */
    auto mock_tx = [](const uint8_t* /*data*/, size_t /*len*/, void* /*user_data*/) -> xgl_error_t {
        return XGL_OK;
    };
    auto mock_rx = [](uint8_t* /*buffer*/, size_t* /*len*/, void* /*user_data*/) -> xgl_error_t {
        return XGL_OK;
    };
    
    xgl_phy_ops_t phy;
    phy.tx = mock_tx;
    phy.rx = mock_rx;
    phy.user_data = NULL;
    
    xgl_route_item_t routes[1];
    routes[0].target_id = 1;
    routes[0].phy = &phy;
    routes[0].max_frame_size = 256;
    routes[0].read_freq_hz = 0;  /* Zero frequency */
    routes[0].metric = 1;
    
    config.route_table = routes;
    config.route_table_len = 1;
    
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglConfigTest, ValidateValidRouteTable) {
    xgl_config_get_default(&config);
    
    /* Mock PHY operations */
    auto mock_tx = [](const uint8_t* /*data*/, size_t /*len*/, void* /*user_data*/) -> xgl_error_t {
        return XGL_OK;
    };
    auto mock_rx = [](uint8_t* /*buffer*/, size_t* /*len*/, void* /*user_data*/) -> xgl_error_t {
        return XGL_OK;
    };
    
    xgl_phy_ops_t phy;
    phy.tx = mock_tx;
    phy.rx = mock_rx;
    phy.user_data = NULL;
    
    xgl_route_item_t routes[1];
    routes[0].target_id = 1;
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
    xgl_config_get_default(&config);
    
    xgl_handle_t handle = xgl_create(&config);
    EXPECT_NE(handle, nullptr);
    
    xgl_destroy(handle);
}

TEST_F(XglConfigTest, CreateInstanceWithInvalidConfig) {
    xgl_config_get_default(&config);
    config.memory.tx_pool_size = 0;  /* Invalid */
    
    xgl_handle_t handle = xgl_create(&config);
    EXPECT_EQ(handle, nullptr);
}
