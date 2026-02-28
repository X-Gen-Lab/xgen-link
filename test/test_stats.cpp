/**
 * \file            test_stats.cpp
 * \brief           Unit tests for statistics API
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Get default configuration */
        xgl_config_get_default(&config);
        config.source_id = 1;
        
        /* Create and initialize instance */
        handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr);
        
        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK);
    }
    
    void TearDown() override {
        if (handle != nullptr) {
            xgl_destroy(handle);
            handle = nullptr;
        }
    }
    
    xgl_config_t config;
    xgl_handle_t handle = nullptr;
};

/*---------------------------------------------------------------------------*/
/* Basic Statistics Tests                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test getting statistics with valid handle
 */
TEST_F(XglStatsTest, GetStatsWithValidHandle) {
    xgl_statistics_t stats;
    
    xgl_error_t err = xgl_stats_get(handle, &stats);
    
    EXPECT_EQ(err, XGL_OK);
}

/**
 * \brief           Test getting statistics with NULL handle
 */
TEST_F(XglStatsTest, GetStatsWithNullHandle) {
    xgl_statistics_t stats;
    
    xgl_error_t err = xgl_stats_get(NULL, &stats);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test getting statistics with NULL stats pointer
 */
TEST_F(XglStatsTest, GetStatsWithNullStatsPointer) {
    xgl_error_t err = xgl_stats_get(handle, NULL);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test getting statistics from uninitialized instance
 */
TEST_F(XglStatsTest, GetStatsFromUninitializedInstance) {
    xgl_config_t temp_config;
    xgl_config_get_default(&temp_config);
    temp_config.source_id = 2;
    
    xgl_handle_t temp_handle = xgl_create(&temp_config);
    ASSERT_NE(temp_handle, nullptr);
    
    xgl_statistics_t stats;
    xgl_error_t err = xgl_stats_get(temp_handle, &stats);
    
    EXPECT_EQ(err, XGL_ERR_NOT_INITIALIZED);
    
    xgl_destroy(temp_handle);
}

/**
 * \brief           Test initial statistics are zero
 */
TEST_F(XglStatsTest, InitialStatsAreZero) {
    xgl_statistics_t stats;
    
    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);
    
    /* Verify all counters are zero */
    EXPECT_EQ(stats.tx_packets, 0);
    EXPECT_EQ(stats.tx_bytes, 0);
    EXPECT_EQ(stats.tx_errors, 0);
    EXPECT_EQ(stats.tx_retries, 0);
    
    EXPECT_EQ(stats.rx_packets, 0);
    EXPECT_EQ(stats.rx_bytes, 0);
    EXPECT_EQ(stats.rx_errors, 0);
    EXPECT_EQ(stats.rx_crc8_errors, 0);
    EXPECT_EQ(stats.rx_crc16_errors, 0);
    EXPECT_EQ(stats.rx_dropped, 0);
    
    EXPECT_EQ(stats.avg_rtt_ms, 0);
    EXPECT_EQ(stats.max_rtt_ms, 0);
    /* min_rtt_ms is initialized to UINT32_MAX to track minimum */
    EXPECT_EQ(stats.min_rtt_ms, UINT32_MAX);
    
    EXPECT_EQ(stats.memory_used, 0);
    EXPECT_EQ(stats.memory_peak, 0);
}

/*---------------------------------------------------------------------------*/
/* Statistics Reset Tests                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test resetting statistics with valid handle
 */
TEST_F(XglStatsTest, ResetStatsWithValidHandle) {
    xgl_error_t err = xgl_stats_reset(handle);
    
    EXPECT_EQ(err, XGL_OK);
}

/**
 * \brief           Test resetting statistics with NULL handle
 */
TEST_F(XglStatsTest, ResetStatsWithNullHandle) {
    xgl_error_t err = xgl_stats_reset(NULL);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test resetting statistics from uninitialized instance
 */
TEST_F(XglStatsTest, ResetStatsFromUninitializedInstance) {
    xgl_config_t temp_config;
    xgl_config_get_default(&temp_config);
    temp_config.source_id = 2;
    
    xgl_handle_t temp_handle = xgl_create(&temp_config);
    ASSERT_NE(temp_handle, nullptr);
    
    xgl_error_t err = xgl_stats_reset(temp_handle);
    
    EXPECT_EQ(err, XGL_ERR_NOT_INITIALIZED);
    
    xgl_destroy(temp_handle);
}

/**
 * \brief           Test statistics remain zero after reset
 */
TEST_F(XglStatsTest, StatsRemainZeroAfterReset) {
    /* Reset statistics */
    xgl_error_t err = xgl_stats_reset(handle);
    ASSERT_EQ(err, XGL_OK);
    
    /* Get statistics */
    xgl_statistics_t stats;
    err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);
    
    /* Verify all counters are zero after reset */
    EXPECT_EQ(stats.tx_packets, 0);
    EXPECT_EQ(stats.tx_bytes, 0);
    EXPECT_EQ(stats.tx_errors, 0);
    EXPECT_EQ(stats.tx_retries, 0);
    
    EXPECT_EQ(stats.rx_packets, 0);
    EXPECT_EQ(stats.rx_bytes, 0);
    EXPECT_EQ(stats.rx_errors, 0);
    EXPECT_EQ(stats.rx_crc8_errors, 0);
    EXPECT_EQ(stats.rx_crc16_errors, 0);
    EXPECT_EQ(stats.rx_dropped, 0);
    
    EXPECT_EQ(stats.avg_rtt_ms, 0);
    EXPECT_EQ(stats.max_rtt_ms, 0);
    /* Note: min_rtt_ms is reset to 0, not UINT32_MAX */
    EXPECT_EQ(stats.min_rtt_ms, 0);
    
    EXPECT_EQ(stats.memory_used, 0);
    EXPECT_EQ(stats.memory_peak, 0);
}

/*---------------------------------------------------------------------------*/
/* Multiple Instance Tests                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test statistics isolation between instances
 */
TEST_F(XglStatsTest, StatsIsolationBetweenInstances) {
    /* Create second instance */
    xgl_config_t config2;
    xgl_config_get_default(&config2);
    config2.source_id = 2;
    
    xgl_handle_t handle2 = xgl_create(&config2);
    ASSERT_NE(handle2, nullptr);
    
    xgl_error_t err = xgl_init(handle2);
    ASSERT_EQ(err, XGL_OK);
    
    /* Get statistics from both instances */
    xgl_statistics_t stats1, stats2;
    
    err = xgl_stats_get(handle, &stats1);
    ASSERT_EQ(err, XGL_OK);
    
    err = xgl_stats_get(handle2, &stats2);
    ASSERT_EQ(err, XGL_OK);
    
    /* Both should have zero statistics */
    EXPECT_EQ(stats1.tx_packets, 0);
    EXPECT_EQ(stats2.tx_packets, 0);
    
    /* Reset first instance */
    err = xgl_stats_reset(handle);
    ASSERT_EQ(err, XGL_OK);
    
    /* Get statistics again */
    err = xgl_stats_get(handle, &stats1);
    ASSERT_EQ(err, XGL_OK);
    
    err = xgl_stats_get(handle2, &stats2);
    ASSERT_EQ(err, XGL_OK);
    
    /* Both should still have zero statistics */
    EXPECT_EQ(stats1.tx_packets, 0);
    EXPECT_EQ(stats2.tx_packets, 0);
    
    xgl_destroy(handle2);
}

/**
 * \brief           Test getting statistics multiple times
 */
TEST_F(XglStatsTest, GetStatsMultipleTimes) {
    xgl_statistics_t stats1, stats2, stats3;
    
    /* Get statistics three times */
    xgl_error_t err1 = xgl_stats_get(handle, &stats1);
    xgl_error_t err2 = xgl_stats_get(handle, &stats2);
    xgl_error_t err3 = xgl_stats_get(handle, &stats3);
    
    EXPECT_EQ(err1, XGL_OK);
    EXPECT_EQ(err2, XGL_OK);
    EXPECT_EQ(err3, XGL_OK);
    
    /* All should be identical */
    EXPECT_EQ(stats1.tx_packets, stats2.tx_packets);
    EXPECT_EQ(stats2.tx_packets, stats3.tx_packets);
    
    EXPECT_EQ(stats1.rx_packets, stats2.rx_packets);
    EXPECT_EQ(stats2.rx_packets, stats3.rx_packets);
}

/**
 * \brief           Test reset and get in sequence
 */
TEST_F(XglStatsTest, ResetAndGetSequence) {
    xgl_statistics_t stats;
    
    /* Reset */
    xgl_error_t err = xgl_stats_reset(handle);
    ASSERT_EQ(err, XGL_OK);
    
    /* Get */
    err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 0);
    
    /* Reset again */
    err = xgl_stats_reset(handle);
    ASSERT_EQ(err, XGL_OK);
    
    /* Get again */
    err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 0);
}
