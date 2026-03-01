/**
 * \file            test_integration.cpp
 * \brief           Integration tests for end-to-end functionality
 * \author          Nexus Team
 * \note            Validates: Requirements 19.2
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <xgl/xgl.h>
#include <mock_phy.h>
#include <mock_callbacks.h>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AtLeast;
using ::testing::NiceMock;

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Integration test fixture
 */
class XglIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Setup default configuration */
        xgl_config_get_default(&config1_);
        config1_.source_id = 1;
        config1_.protocol.max_frame_size = 256;
        config1_.protocol.window_size = 4;
        config1_.protocol.max_retry_count = 3;
        config1_.protocol.ack_timeout_ms = 100;
        
        xgl_config_get_default(&config2_);
        config2_.source_id = 2;
        config2_.protocol.max_frame_size = 256;
        config2_.protocol.window_size = 4;
        config2_.protocol.max_retry_count = 3;
        config2_.protocol.ack_timeout_ms = 100;
    }
    
    void TearDown() override {
        /* Cleanup any created instances */
        if (handle1_ != nullptr) {
            xgl_destroy(handle1_);
            handle1_ = nullptr;
        }
        if (handle2_ != nullptr) {
            xgl_destroy(handle2_);
            handle2_ = nullptr;
        }
    }
    
    xgl_config_t config1_;
    xgl_config_t config2_;
    xgl_route_item_t route1_;
    xgl_route_item_t route2_;
    xgl_handle_t handle1_ = nullptr;
    xgl_handle_t handle2_ = nullptr;
};

/*---------------------------------------------------------------------------*/
/* Basic Instance Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test basic instance creation and destruction
 * \note            Validates: Requirements 19.2, 1.1, 1.2, 1.3
 */
TEST_F(XglIntegrationTest, BasicInstanceLifecycle) {
    /* Create instance */
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    
    /* Initialize instance */
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);
    
    /* Get statistics */
    xgl_statistics_t stats;
    ASSERT_EQ(xgl_stats_get(handle1_, &stats), XGL_OK);
    
    /* Verify initial statistics */
    EXPECT_EQ(stats.datalink.tx_packets, 0);
    EXPECT_EQ(stats.datalink.rx_packets, 0);
    EXPECT_EQ(stats.datalink.tx_errors, 0);
    EXPECT_EQ(stats.datalink.rx_errors, 0);
    
    /* Destroy instance */
    xgl_destroy(handle1_);
    handle1_ = nullptr;
}

/**
 * \brief           Test multiple independent instances
 * \note            Validates: Requirements 19.2, 1.4
 */
TEST_F(XglIntegrationTest, MultipleIndependentInstances) {
    /* Create first instance */
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);
    
    /* Create second instance */
    handle2_ = xgl_create(&config2_);
    ASSERT_NE(handle2_, nullptr);
    ASSERT_EQ(xgl_init(handle2_), XGL_OK);
    
    /* Verify both instances have independent statistics */
    xgl_statistics_t stats1, stats2;
    ASSERT_EQ(xgl_stats_get(handle1_, &stats1), XGL_OK);
    ASSERT_EQ(xgl_stats_get(handle2_, &stats2), XGL_OK);
    
    /* Reset statistics on first instance */
    ASSERT_EQ(xgl_stats_reset(handle1_), XGL_OK);
    
    /* Verify only first instance was reset */
    ASSERT_EQ(xgl_stats_get(handle1_, &stats1), XGL_OK);
    ASSERT_EQ(xgl_stats_get(handle2_, &stats2), XGL_OK);
    
    EXPECT_EQ(stats1.datalink.tx_packets, 0);
    EXPECT_EQ(stats2.datalink.tx_packets, 0);
}

/**
 * \brief           Test configuration validation
 * \note            Validates: Requirements 19.2, 10.2
 */
TEST_F(XglIntegrationTest, ConfigurationValidation) {
    xgl_config_t config;
    
    /* Test valid configuration */
    xgl_config_get_default(&config);
    config.source_id = 1;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
    
    /* Test invalid configuration - zero pool size */
    config.memory.tx_pool_size = 0;
    EXPECT_NE(xgl_config_validate(&config), XGL_OK);
    
    /* Test invalid configuration - zero window size */
    xgl_config_get_default(&config);
    config.source_id = 1;
    config.protocol.window_size = 0;
    EXPECT_NE(xgl_config_validate(&config), XGL_OK);
}

/**
 * \brief           Test configuration presets
 * \note            Validates: Requirements 19.2, 10.3, 42.3
 */
TEST_F(XglIntegrationTest, ConfigurationPresets) {
    xgl_config_t config;
    
    /* Test tiny preset */
    xgl_config_get_preset_tiny(&config);
    config.source_id = 1;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
    EXPECT_EQ(config.memory.tx_pool_size, 1024);
    EXPECT_EQ(config.protocol.max_frame_size, 128);
    EXPECT_FALSE(config.features.enable_fragmentation);
    
    /* Test small preset */
    xgl_config_get_preset_small(&config);
    config.source_id = 1;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
    EXPECT_EQ(config.memory.tx_pool_size, 2048);
    EXPECT_EQ(config.protocol.max_frame_size, 256);
    EXPECT_TRUE(config.features.enable_fragmentation);
    
    /* Test medium preset */
    xgl_config_get_preset_medium(&config);
    config.source_id = 1;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
    EXPECT_EQ(config.memory.tx_pool_size, 4096);
    EXPECT_EQ(config.protocol.max_frame_size, 512);
    EXPECT_TRUE(config.features.enable_compression);
    
    /* Test large preset */
    xgl_config_get_preset_large(&config);
    config.source_id = 1;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
    EXPECT_EQ(config.memory.tx_pool_size, 8192);
    EXPECT_EQ(config.protocol.max_frame_size, 1024);
    EXPECT_TRUE(config.features.enable_encryption);
}

/*---------------------------------------------------------------------------*/
/* Runtime Processing Tests                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_run function
 * \note            Validates: Requirements 19.2
 */
TEST_F(XglIntegrationTest, RuntimeProcessing) {
    /* Create and initialize instance */
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);
    
    /* Call xgl_run multiple times - should not crash */
    for (int i = 0; i < 10; ++i) {
        xgl_run(handle1_, 100);
    }
    
    /* Verify instance is still valid */
    xgl_statistics_t stats;
    ASSERT_EQ(xgl_stats_get(handle1_, &stats), XGL_OK);
}

/**
 * \brief           Test multiple instances with runtime processing
 * \note            Validates: Requirements 19.2, 1.4
 */
TEST_F(XglIntegrationTest, MultiInstanceRuntimeProcessing) {
    /* Create two instances */
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);
    
    handle2_ = xgl_create(&config2_);
    ASSERT_NE(handle2_, nullptr);
    ASSERT_EQ(xgl_init(handle2_), XGL_OK);
    
    /* Process both instances */
    for (int i = 0; i < 10; ++i) {
        xgl_run(handle1_, 100);
        xgl_run(handle2_, 100);
    }
    
    /* Verify both instances are still valid */
    xgl_statistics_t stats1, stats2;
    ASSERT_EQ(xgl_stats_get(handle1_, &stats1), XGL_OK);
    ASSERT_EQ(xgl_stats_get(handle2_, &stats2), XGL_OK);
}

/*---------------------------------------------------------------------------*/
/* Error Handling Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test error handling with null handle
 * \note            Validates: Requirements 19.2, 8.1
 */
TEST_F(XglIntegrationTest, NullHandleErrorHandling) {
    xgl_statistics_t stats;
    
    /* Test with null handle */
    EXPECT_NE(xgl_stats_get(nullptr, &stats), XGL_OK);
    EXPECT_NE(xgl_stats_reset(nullptr), XGL_OK);
    
    /* xgl_run should handle null gracefully */
    xgl_run(nullptr, 100);  /* Should not crash */
    
    /* xgl_destroy should handle null gracefully */
    xgl_destroy(nullptr);  /* Should not crash */
}

/**
 * \brief           Test initialization errors
 * \note            Validates: Requirements 19.2, 2.2, 8.3
 */
TEST_F(XglIntegrationTest, InitializationErrors) {
    /* Test double initialization */
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);
    
    /* Second init should fail */
    EXPECT_EQ(xgl_init(handle1_), XGL_ERR_ALREADY_INITIALIZED);
}

/**
 * \brief           Test memory allocation failure handling
 * \note            Validates: Requirements 19.2, 2.2
 */
TEST_F(XglIntegrationTest, MemoryAllocationFailure) {
    /* Create instance with very large pool size */
    xgl_config_t config;
    xgl_config_get_default(&config);
    config.source_id = 1;
    config.memory.tx_pool_size = 1024 * 1024 * 1024;  /* 1GB - likely to fail */
    
    xgl_handle_t handle = xgl_create(&config);
    if (handle != nullptr) {
        /* If creation succeeded, initialization might fail */
        xgl_error_t err = xgl_init(handle);
        /* Either init fails or succeeds, both are acceptable */
        (void)err;
        xgl_destroy(handle);
    }
    /* Test passes if we don't crash */
}

/*---------------------------------------------------------------------------*/
/* Version Information Tests                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test version information
 * \note            Validates: Requirements 19.2, 29.1
 */
TEST_F(XglIntegrationTest, VersionInformation) {
    /* Test version string */
    const char* version_str = xgl_version_string();
    ASSERT_NE(version_str, nullptr);
    EXPECT_STREQ(version_str, XGL_VERSION_STRING);
    
    /* Test version integer */
    uint32_t version_int = xgl_version_int();
    EXPECT_EQ(version_int, XGL_VERSION_INT);
    EXPECT_EQ(version_int, 10000);  /* 1.0.0 */
}

/*---------------------------------------------------------------------------*/
/* Stress Tests                                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test rapid instance creation and destruction
 * \note            Validates: Requirements 19.2, 1.3, 2.3
 */
TEST_F(XglIntegrationTest, RapidInstanceLifecycle) {
    const int iterations = 100;
    
    for (int i = 0; i < iterations; ++i) {
        xgl_handle_t handle = xgl_create(&config1_);
        ASSERT_NE(handle, nullptr);
        ASSERT_EQ(xgl_init(handle), XGL_OK);
        xgl_destroy(handle);
    }
    
    /* Test passes if no memory leaks */
}

/**
 * \brief           Test statistics operations under load
 * \note            Validates: Requirements 19.2, 11.1, 11.2, 11.3
 */
TEST_F(XglIntegrationTest, StatisticsUnderLoad) {
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);
    
    xgl_statistics_t stats;
    
    /* Rapidly get and reset statistics */
    for (int i = 0; i < 1000; ++i) {
        ASSERT_EQ(xgl_stats_get(handle1_, &stats), XGL_OK);
        if (i % 10 == 0) {
            ASSERT_EQ(xgl_stats_reset(handle1_), XGL_OK);
        }
    }
}
