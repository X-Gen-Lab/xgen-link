/**
 * \file            test_instance.cpp
 * \brief           Unit tests for protocol instance management
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglInstanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Get default configuration */
        xgl_config_get_default(&config);
        config.source_id = 1;
    }
    
    void TearDown() override {
        /* Cleanup handled by individual tests */
    }
    
    xgl_config_t config;
};

/*---------------------------------------------------------------------------*/
/* Basic Instance Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance creation with valid configuration
 */
TEST_F(XglInstanceTest, CreateWithValidConfig) {
    xgl_handle_t handle = xgl_create(&config);
    
    ASSERT_NE(handle, nullptr);
    
    xgl_destroy(handle);
}

/**
 * \brief           Test instance creation with NULL configuration
 */
TEST_F(XglInstanceTest, CreateWithNullConfig) {
    xgl_handle_t handle = xgl_create(NULL);
    
    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid TX pool size
 */
TEST_F(XglInstanceTest, CreateWithInvalidTxPoolSize) {
    config.tx_pool_size = 0;
    
    xgl_handle_t handle = xgl_create(&config);
    
    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid RX buffer size
 */
TEST_F(XglInstanceTest, CreateWithInvalidRxBufferSize) {
    config.rx_buffer_size = 0;
    
    xgl_handle_t handle = xgl_create(&config);
    
    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid ACK timeout
 */
TEST_F(XglInstanceTest, CreateWithInvalidAckTimeout) {
    config.ack_timeout_ms = 0;
    
    xgl_handle_t handle = xgl_create(&config);
    
    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid max frame size
 */
TEST_F(XglInstanceTest, CreateWithInvalidMaxFrameSize) {
    config.max_frame_size = 0;
    
    xgl_handle_t handle = xgl_create(&config);
    
    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with frame size too small
 */
TEST_F(XglInstanceTest, CreateWithFrameSizeTooSmall) {
    config.max_frame_size = 10;  /* Less than header + CRC */
    
    xgl_handle_t handle = xgl_create(&config);
    
    EXPECT_EQ(handle, nullptr);
}

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance initialization
 */
TEST_F(XglInstanceTest, InitializeInstance) {
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    
    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_destroy(handle);
}

/**
 * \brief           Test initialization with NULL handle
 */
TEST_F(XglInstanceTest, InitializeNullHandle) {
    xgl_error_t err = xgl_init(NULL);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test double initialization
 */
TEST_F(XglInstanceTest, DoubleInitialization) {
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    
    xgl_error_t err1 = xgl_init(handle);
    EXPECT_EQ(err1, XGL_OK);
    
    xgl_error_t err2 = xgl_init(handle);
    EXPECT_EQ(err2, XGL_ERR_ALREADY_INITIALIZED);
    
    xgl_destroy(handle);
}

/*---------------------------------------------------------------------------*/
/* Destruction Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance destruction
 */
TEST_F(XglInstanceTest, DestroyInstance) {
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    
    xgl_error_t err = xgl_init(handle);
    ASSERT_EQ(err, XGL_OK);
    
    /* Should not crash */
    xgl_destroy(handle);
}

/**
 * \brief           Test destruction with NULL handle
 */
TEST_F(XglInstanceTest, DestroyNullHandle) {
    /* Should not crash */
    xgl_destroy(NULL);
}

/**
 * \brief           Test destruction without initialization
 */
TEST_F(XglInstanceTest, DestroyWithoutInit) {
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    
    /* Should not crash even if not initialized */
    xgl_destroy(handle);
}

/*---------------------------------------------------------------------------*/
/* Configuration Preset Tests                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test tiny configuration preset
 */
TEST_F(XglInstanceTest, TinyConfigPreset) {
    xgl_config_t tiny_config = XGL_CONFIG_PRESET_TINY;
    tiny_config.source_id = 1;
    
    xgl_handle_t handle = xgl_create(&tiny_config);
    ASSERT_NE(handle, nullptr);
    
    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_destroy(handle);
}

/**
 * \brief           Test small configuration preset
 */
TEST_F(XglInstanceTest, SmallConfigPreset) {
    xgl_config_t small_config = XGL_CONFIG_PRESET_SMALL;
    small_config.source_id = 1;
    
    xgl_handle_t handle = xgl_create(&small_config);
    ASSERT_NE(handle, nullptr);
    
    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_destroy(handle);
}

/**
 * \brief           Test medium configuration preset
 */
TEST_F(XglInstanceTest, MediumConfigPreset) {
    xgl_config_t medium_config = XGL_CONFIG_PRESET_MEDIUM;
    medium_config.source_id = 1;
    
    xgl_handle_t handle = xgl_create(&medium_config);
    ASSERT_NE(handle, nullptr);
    
    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_destroy(handle);
}

/**
 * \brief           Test large configuration preset
 */
TEST_F(XglInstanceTest, LargeConfigPreset) {
    xgl_config_t large_config = XGL_CONFIG_PRESET_LARGE;
    large_config.source_id = 1;
    
    xgl_handle_t handle = xgl_create(&large_config);
    ASSERT_NE(handle, nullptr);
    
    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_destroy(handle);
}

/*---------------------------------------------------------------------------*/
/* Multiple Instance Tests                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test creating multiple instances
 */
TEST_F(XglInstanceTest, MultipleInstances) {
    xgl_config_t config1 = config;
    xgl_config_t config2 = config;
    
    config1.source_id = 1;
    config2.source_id = 2;
    
    xgl_handle_t handle1 = xgl_create(&config1);
    xgl_handle_t handle2 = xgl_create(&config2);
    
    ASSERT_NE(handle1, nullptr);
    ASSERT_NE(handle2, nullptr);
    EXPECT_NE(handle1, handle2);
    
    xgl_error_t err1 = xgl_init(handle1);
    xgl_error_t err2 = xgl_init(handle2);
    
    EXPECT_EQ(err1, XGL_OK);
    EXPECT_EQ(err2, XGL_OK);
    
    xgl_destroy(handle1);
    xgl_destroy(handle2);
}

/*---------------------------------------------------------------------------*/
/* Route Configuration Tests                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance with route table
 */
TEST_F(XglInstanceTest, InstanceWithRouteTable) {
    /* Create mock PHY operations */
    xgl_phy_ops_t phy = {
        .tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
            (void)data;
            (void)len;
            (void)user_data;
            return XGL_OK;
        },
        .rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
            (void)buffer;
            (void)user_data;
            *len = 0;  /* No data available */
            return XGL_OK;
        },
        .user_data = NULL
    };
    
    xgl_route_item_t routes[] = {
        {
            .target_id = 2,
            .phy = &phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 100
        },
        {
            .target_id = 3,
            .phy = &phy,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 100
        }
    };
    
    config.route_table = routes;
    config.route_table_len = 2;
    
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    
    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_destroy(handle);
}

/**
 * \brief           Test instance with invalid route table
 */
TEST_F(XglInstanceTest, InstanceWithInvalidRouteTable) {
    config.route_table = NULL;
    config.route_table_len = 5;  /* Non-zero length but NULL pointer */
    
    xgl_handle_t handle = xgl_create(&config);
    
    EXPECT_EQ(handle, nullptr);
}
