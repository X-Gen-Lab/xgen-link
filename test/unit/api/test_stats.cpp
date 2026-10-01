/**
 * \file            test_stats.cpp
 * \brief           Unit tests for statistics API
 * \author          X-Gen Lab
 */

#include "test_host_allocator.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <vector>
#include <xgl/xgl.h>

namespace {

struct StatisticsPort {
    std::vector<uint8_t> incoming;
    StatisticsPort* remote = nullptr;
};

xgl_error_t statistics_tx(const uint8_t* data, size_t size, void* context) {
    auto& port = *static_cast<StatisticsPort*>(context);
    port.remote->incoming.insert(port.remote->incoming.end(), data,
                                 data + size);
    return XGL_OK;
}

xgl_error_t statistics_rx(uint8_t* data, size_t* size, void* context) {
    auto& port = *static_cast<StatisticsPort*>(context);
    *size = std::min(*size, port.incoming.size());
    std::copy_n(port.incoming.begin(), *size, data);
    port.incoming.erase(port.incoming.begin(),
                        port.incoming.begin() + static_cast<ptrdiff_t>(*size));
    return XGL_OK;
}

}  // namespace

TEST(XglStatisticsBehavior,
     ReportsAcceptedAckSamplesAndResetKeepsReservations) {
    StatisticsPort first, second;
    first.remote = &second;
    second.remote = &first;
    xgl_phy_ops_t first_phy = {statistics_tx, statistics_rx, &first};
    xgl_phy_ops_t second_phy = {statistics_tx, statistics_rx, &second};
    xgl_route_item_t first_route = {2U, &first_phy, 128U, 1000U, 1U};
    xgl_route_item_t second_route = {1U, &second_phy, 128U, 1000U, 1U};
    xgl_config_t sender_config, receiver_config;
    xgl_config_get_preset_tiny(&sender_config);
    xgl_config_get_preset_tiny(&receiver_config);
    sender_config.source_id = 1U;
    receiver_config.source_id = 2U;
    sender_config.route_table = &first_route;
    receiver_config.route_table = &second_route;
    sender_config.route_table_len = receiver_config.route_table_len = 1U;
    xgl_test_use_host_allocator(&sender_config);
    xgl_test_use_host_allocator(&receiver_config);
    xgl_handle_t sender = xgl_create(&sender_config);
    xgl_handle_t receiver = xgl_create(&receiver_config);
    ASSERT_NE(sender, nullptr);
    ASSERT_NE(receiver, nullptr);
    ASSERT_EQ(xgl_init(sender), XGL_OK);
    ASSERT_EQ(xgl_init(receiver), XGL_OK);

    const uint8_t payload = 42U;
    xgl_tx_data_t tx{};
    tx.target_id = 2U;
    tx.data = &payload;
    tx.data_len = 1U;
    tx.reliable = true;
    const xgl_work_budget_t budget = {128U, 1000U};
    auto round_trip = [&](uint32_t sent_at, uint32_t acknowledged_at) {
        ASSERT_EQ(xgl_send_at(sender, &tx, sent_at), XGL_OK);
        for (uint32_t i = 0; i < 8U && !second.incoming.empty(); ++i) {
            ASSERT_EQ(xgl_step(receiver, sent_at + 1U + i, &budget), XGL_OK);
        }
        ASSERT_TRUE(second.incoming.empty());
        ASSERT_FALSE(first.incoming.empty());
        for (uint32_t i = 0; i < 8U && !first.incoming.empty(); ++i) {
            ASSERT_EQ(xgl_step(sender, acknowledged_at + i, &budget), XGL_OK);
        }
        ASSERT_TRUE(first.incoming.empty());
    };
    round_trip(100U, 150U);
    round_trip(200U, 300U);
    xgl_statistics_t stats{};
    ASSERT_EQ(xgl_stats_get(sender, &stats), XGL_OK);
    EXPECT_EQ(stats.avg_rtt_ms, 75U);
    EXPECT_EQ(stats.min_rtt_ms, 50U);
    EXPECT_EQ(stats.max_rtt_ms, 100U);
    xgl_memory_requirements_t memory{};
    ASSERT_EQ(xgl_memory_requirements(&sender_config, &memory), XGL_OK);
    EXPECT_EQ(stats.memory_used, memory.size);
    EXPECT_EQ(stats.memory_peak, memory.size);

    ASSERT_EQ(xgl_stats_reset(sender), XGL_OK);
    ASSERT_EQ(xgl_stats_get(sender, &stats), XGL_OK);
    EXPECT_EQ(stats.avg_rtt_ms, 0U);
    EXPECT_EQ(stats.min_rtt_ms, UINT32_MAX);
    EXPECT_EQ(stats.max_rtt_ms, 0U);
    EXPECT_EQ(stats.memory_used, memory.size);
    round_trip(400U, 440U);
    ASSERT_EQ(xgl_stats_get(sender, &stats), XGL_OK);
    EXPECT_EQ(stats.avg_rtt_ms, 40U);
    EXPECT_EQ(stats.min_rtt_ms, 40U);
    EXPECT_EQ(stats.max_rtt_ms, 40U);
    xgl_destroy(receiver);
    xgl_destroy(sender);
}

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
        xgl_test_use_host_allocator(&config);
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

    xgl_test_use_host_allocator(&temp_config);
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
    EXPECT_EQ(stats.datalink.tx_packets, 0);
    EXPECT_EQ(stats.datalink.tx_bytes, 0);
    EXPECT_EQ(stats.datalink.tx_errors, 0);
    EXPECT_EQ(stats.tx_retries, 0);

    EXPECT_EQ(stats.datalink.rx_packets, 0);
    EXPECT_EQ(stats.datalink.rx_bytes, 0);
    EXPECT_EQ(stats.datalink.rx_errors, 0);
    EXPECT_EQ(stats.rx_header_crc_errors, 0);
    EXPECT_EQ(stats.rx_crc16_errors, 0);
    EXPECT_EQ(stats.datalink.rx_dropped, 0);

    EXPECT_EQ(stats.avg_rtt_ms, 0);
    EXPECT_EQ(stats.max_rtt_ms, 0);
    /* min_rtt_ms is initialized to UINT32_MAX to track minimum */
    EXPECT_EQ(stats.min_rtt_ms, UINT32_MAX);

    xgl_memory_requirements_t memory{};
    ASSERT_EQ(xgl_memory_requirements(&config, &memory), XGL_OK);
    EXPECT_EQ(stats.memory_used, memory.size);
    EXPECT_EQ(stats.memory_peak, memory.size);
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

    xgl_test_use_host_allocator(&temp_config);
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
    EXPECT_EQ(stats.datalink.tx_packets, 0);
    EXPECT_EQ(stats.datalink.tx_bytes, 0);
    EXPECT_EQ(stats.datalink.tx_errors, 0);
    EXPECT_EQ(stats.tx_retries, 0);

    EXPECT_EQ(stats.datalink.rx_packets, 0);
    EXPECT_EQ(stats.datalink.rx_bytes, 0);
    EXPECT_EQ(stats.datalink.rx_errors, 0);
    EXPECT_EQ(stats.rx_header_crc_errors, 0);
    EXPECT_EQ(stats.rx_crc16_errors, 0);
    EXPECT_EQ(stats.datalink.rx_dropped, 0);

    EXPECT_EQ(stats.avg_rtt_ms, 0);
    EXPECT_EQ(stats.max_rtt_ms, 0);
    /* An empty sample set has the same sentinel before and after reset. */
    EXPECT_EQ(stats.min_rtt_ms, UINT32_MAX);

    xgl_memory_requirements_t memory{};
    ASSERT_EQ(xgl_memory_requirements(&config, &memory), XGL_OK);
    EXPECT_EQ(stats.memory_used, memory.size);
    EXPECT_EQ(stats.memory_peak, memory.size);
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

    xgl_test_use_host_allocator(&config2);
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
    EXPECT_EQ(stats1.datalink.tx_packets, 0);
    EXPECT_EQ(stats2.datalink.tx_packets, 0);

    /* Reset first instance */
    err = xgl_stats_reset(handle);
    ASSERT_EQ(err, XGL_OK);

    /* Get statistics again */
    err = xgl_stats_get(handle, &stats1);
    ASSERT_EQ(err, XGL_OK);

    err = xgl_stats_get(handle2, &stats2);
    ASSERT_EQ(err, XGL_OK);

    /* Both should still have zero statistics */
    EXPECT_EQ(stats1.datalink.tx_packets, 0);
    EXPECT_EQ(stats2.datalink.tx_packets, 0);

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
    EXPECT_EQ(stats1.datalink.tx_packets, stats2.datalink.tx_packets);
    EXPECT_EQ(stats2.datalink.tx_packets, stats3.datalink.tx_packets);

    EXPECT_EQ(stats1.datalink.rx_packets, stats2.datalink.rx_packets);
    EXPECT_EQ(stats2.datalink.rx_packets, stats3.datalink.rx_packets);
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
    EXPECT_EQ(stats.datalink.tx_packets, 0);

    /* Reset again */
    err = xgl_stats_reset(handle);
    ASSERT_EQ(err, XGL_OK);

    /* Get again */
    err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.datalink.tx_packets, 0);
}
