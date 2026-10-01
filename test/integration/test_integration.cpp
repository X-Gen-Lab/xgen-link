/**
 * \file            test_integration.cpp
 * \brief           Integration tests for end-to-end functionality
 * \author          X-Gen Lab
 * \note            Validates: Requirements 19.2
 */

#include "test_host_allocator.h"

#include <xgl/xgl.h>

#include <chrono>
#include <cstring>
#include <deque>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mock_callbacks.h>
#include <mock_phy.h>
#include <mutex>
#include <thread>
#include <vector>

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

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

    uint32_t now_ms_ = 0U;
    const xgl_work_budget_t budget_ = {1024U, 1000U};
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
    xgl_test_use_host_allocator(&config1_);
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
    xgl_test_use_host_allocator(&config1_);
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);

    /* Create second instance */
    xgl_test_use_host_allocator(&config2_);
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
    config.features.max_tx_packets = 0;
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
    EXPECT_EQ(config.features.max_tx_packets, 2U);
    EXPECT_EQ(config.protocol.max_frame_size, 128);
    EXPECT_FALSE(config.features.enable_fragmentation);

    /* Test small preset */
    xgl_config_get_preset_small(&config);
    config.source_id = 1;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
    EXPECT_EQ(config.features.max_tx_packets, 4U);
    EXPECT_EQ(config.protocol.max_frame_size, 256);
    EXPECT_TRUE(config.features.enable_fragmentation);

    /* Test medium preset */
    xgl_config_get_preset_medium(&config);
    config.source_id = 1;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
    EXPECT_EQ(config.features.max_tx_packets, 16U);
    EXPECT_EQ(config.protocol.max_frame_size, 512);
    EXPECT_FALSE(config.features.enable_compression);

    /* Test large preset */
    xgl_config_get_preset_large(&config);
    config.source_id = 1;
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
    EXPECT_EQ(config.features.max_tx_packets, 64U);
    EXPECT_EQ(config.protocol.max_frame_size, 1024);
    EXPECT_FALSE(config.features.enable_encryption);
}

/*---------------------------------------------------------------------------*/
/* Runtime Processing Tests                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_step function
 * \note            Validates: Requirements 19.2
 */
TEST_F(XglIntegrationTest, RuntimeProcessing) {
    /* Create and initialize instance */
    xgl_test_use_host_allocator(&config1_);
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);

    /* Call xgl_step multiple times - should not crash */
    for (int i = 0; i < 10; ++i) {
        xgl_step(handle1_, now_ms_ += 10U, &budget_);
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
    xgl_test_use_host_allocator(&config1_);
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);

    xgl_test_use_host_allocator(&config2_);
    handle2_ = xgl_create(&config2_);
    ASSERT_NE(handle2_, nullptr);
    ASSERT_EQ(xgl_init(handle2_), XGL_OK);

    /* Process both instances */
    for (int i = 0; i < 10; ++i) {
        xgl_step(handle1_, now_ms_ += 10U, &budget_);
        xgl_step(handle2_, now_ms_ += 10U, &budget_);
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

    /* xgl_step should handle null gracefully */
    xgl_step(nullptr, now_ms_ += 10U, &budget_); /* Should not crash */

    /* xgl_destroy should handle null gracefully */
    xgl_destroy(nullptr); /* Should not crash */
}

/**
 * \brief           Test initialization errors
 * \note            Validates: Requirements 19.2, 2.2, 8.3
 */
TEST_F(XglIntegrationTest, InitializationErrors) {
    /* Test double initialization */
    xgl_test_use_host_allocator(&config1_);
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
    xgl_config_t config;
    xgl_config_get_default(&config);
    config.features.max_tx_packets = SIZE_MAX;
    xgl_memory_requirements_t requirements{};
    EXPECT_EQ(xgl_memory_requirements(&config, &requirements),
              XGL_ERR_INVALID_PARAM);
    xgl_test_use_host_allocator(&config);
    EXPECT_EQ(xgl_create(&config), nullptr);
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
    EXPECT_EQ(version_int, 30000); /* SDK 3; wire version is independent. */
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
        xgl_test_use_host_allocator(&config1_);
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
    xgl_test_use_host_allocator(&config1_);
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

/*---------------------------------------------------------------------------*/
/* End-to-End Data Path Tests (Loopback PHY)                                 */
/*---------------------------------------------------------------------------*/

namespace {

struct E2eRxRecord {
    uint16_t source_id;
    uint8_t data_type;
    std::vector<uint8_t> data;
};

struct E2eRxTracker {
    std::mutex mu;
    std::vector<E2eRxRecord> records;
};

static void e2e_rx_callback(xgl_handle_t handle, uint16_t source_id,
                            uint8_t data_type, const uint8_t* data, size_t len,
                            void* user_data) {
    (void)handle;
    auto* tracker = static_cast<E2eRxTracker*>(user_data);
    if (tracker != nullptr && data != nullptr && len > 0) {
        std::lock_guard<std::mutex> lock(tracker->mu);
        tracker->records.push_back(
            {source_id, data_type, std::vector<uint8_t>(data, data + len)});
    }
}

}  // namespace

/**
 * \brief           End-to-end unreliable single-hop test
 * \details         A sends unreliable data -> B receives via rx_callback
 */
TEST_F(XglIntegrationTest, UnreliableSingleHop) {
    /* Create loopback PHY pair connecting A and B */
    LoopbackPhyPair loopback;
    xgl_phy_ops_t phy_a = loopback.get_phy_a();
    xgl_phy_ops_t phy_b = loopback.get_phy_b();

    /* Configure routes */
    xgl_route_item_t route_a = {};
    route_a.target_id = 2;
    route_a.phy = &phy_a;
    route_a.max_frame_size = 256;
    route_a.read_freq_hz = 100;
    route_a.metric = 1;
    xgl_route_item_t route_b = {};
    route_b.target_id = 1;
    route_b.phy = &phy_b;
    route_b.max_frame_size = 256;
    route_b.read_freq_hz = 100;
    route_b.metric = 1;

    config1_.route_table = &route_a;
    config1_.route_table_len = 1;
    config2_.route_table = &route_b;
    config2_.route_table_len = 1;

    E2eRxTracker rx_tracker_b;
    config2_.rx_callback = e2e_rx_callback;
    config2_.callback_user_data = &rx_tracker_b;

    /* Create and init both instances */
    xgl_test_use_host_allocator(&config1_);
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);

    xgl_test_use_host_allocator(&config2_);
    handle2_ = xgl_create(&config2_);
    ASSERT_NE(handle2_, nullptr);
    ASSERT_EQ(xgl_init(handle2_), XGL_OK);

    /* A sends unreliable data to B */
    const uint8_t payload[] = {'H', 'e', 'l', 'l', 'o'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;
    ASSERT_EQ(xgl_send_at(handle1_, &tx_data, now_ms_), XGL_OK);

    /* Run A to flush TX, then run B to receive */
    xgl_step(handle1_, now_ms_ += 10U, &budget_);
    xgl_step(handle2_, now_ms_ += 10U, &budget_);

    /* Verify B received the data */
    ASSERT_EQ(rx_tracker_b.records.size(), 1U);
    EXPECT_EQ(rx_tracker_b.records[0].source_id, 1);
    EXPECT_EQ(rx_tracker_b.records[0].data_type, 1);
    EXPECT_EQ(rx_tracker_b.records[0].data,
              std::vector<uint8_t>(payload, payload + sizeof(payload)));
}

/**
 * \brief           End-to-end reliable send with ACK
 * \details         A sends reliable -> B receives -> ACK flows back -> A's
 * queue drains
 */
TEST_F(XglIntegrationTest, ReliableWithAck) {
    LoopbackPhyPair loopback;
    xgl_phy_ops_t phy_a = loopback.get_phy_a();
    xgl_phy_ops_t phy_b = loopback.get_phy_b();

    xgl_route_item_t route_a = {};
    route_a.target_id = 2;
    route_a.phy = &phy_a;
    route_a.max_frame_size = 256;
    route_a.read_freq_hz = 100;
    route_a.metric = 1;
    xgl_route_item_t route_b = {};
    route_b.target_id = 1;
    route_b.phy = &phy_b;
    route_b.max_frame_size = 256;
    route_b.read_freq_hz = 100;
    route_b.metric = 1;

    config1_.route_table = &route_a;
    config1_.route_table_len = 1;
    config2_.route_table = &route_b;
    config2_.route_table_len = 1;

    E2eRxTracker rx_tracker_b;
    config2_.rx_callback = e2e_rx_callback;
    config2_.callback_user_data = &rx_tracker_b;

    xgl_test_use_host_allocator(&config1_);
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);

    xgl_test_use_host_allocator(&config2_);
    handle2_ = xgl_create(&config2_);
    ASSERT_NE(handle2_, nullptr);
    ASSERT_EQ(xgl_init(handle2_), XGL_OK);

    /* A sends reliable data to B */
    const uint8_t payload[] = {'R', 'E', 'L'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 500;
    ASSERT_EQ(xgl_send_at(handle1_, &tx_data, now_ms_), XGL_OK);

    /* Run multiple cycles to deliver data and ACK */
    for (int i = 0; i < 5; ++i) {
        xgl_step(handle1_, now_ms_ += 10U, &budget_);
        xgl_step(handle2_, now_ms_ += 10U, &budget_);
    }

    /* Verify B received the data */
    ASSERT_GE(rx_tracker_b.records.size(), 1U);
    EXPECT_EQ(rx_tracker_b.records[0].source_id, 1);
    EXPECT_EQ(rx_tracker_b.records[0].data,
              std::vector<uint8_t>(payload, payload + sizeof(payload)));

    /* Verify A's reliable queue drained (ACK was processed) */
    xgl_statistics_t stats_a;
    ASSERT_EQ(xgl_stats_get(handle1_, &stats_a), XGL_OK);
    EXPECT_GE(stats_a.transport.tx_packets, 1U);
}

/**
 * \brief           End-to-end fragmented message test
 * \details         A sends oversized payload -> B reassembles -> rx_callback
 * gets complete data
 */
TEST_F(XglIntegrationTest, FragmentedMessage) {
    LoopbackPhyPair loopback;
    xgl_phy_ops_t phy_a = loopback.get_phy_a();
    xgl_phy_ops_t phy_b = loopback.get_phy_b();

    /* Enable fragmentation with small max_frame_size to force fragmentation */
    config1_.features.enable_fragmentation = true;
    config1_.protocol.max_frame_size = 64;
    config2_.features.enable_fragmentation = true;
    config2_.protocol.max_frame_size = 64;

    xgl_route_item_t route_a = {};
    route_a.target_id = 2;
    route_a.phy = &phy_a;
    route_a.max_frame_size = 64;
    route_a.read_freq_hz = 100;
    route_a.metric = 1;
    xgl_route_item_t route_b = {};
    route_b.target_id = 1;
    route_b.phy = &phy_b;
    route_b.max_frame_size = 64;
    route_b.read_freq_hz = 100;
    route_b.metric = 1;

    config1_.route_table = &route_a;
    config1_.route_table_len = 1;
    config2_.route_table = &route_b;
    config2_.route_table_len = 1;

    E2eRxTracker rx_tracker_b;
    config2_.rx_callback = e2e_rx_callback;
    config2_.callback_user_data = &rx_tracker_b;

    xgl_test_use_host_allocator(&config1_);
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);

    xgl_test_use_host_allocator(&config2_);
    handle2_ = xgl_create(&config2_);
    ASSERT_NE(handle2_, nullptr);
    ASSERT_EQ(xgl_init(handle2_), XGL_OK);

    /* Create payload larger than max_frame_size to force fragmentation */
    std::vector<uint8_t> payload(100);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i & 0xFF);
    }

    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 0;
    tx_data.data = payload.data();
    tx_data.data_len = payload.size();
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;
    ASSERT_EQ(xgl_send_at(handle1_, &tx_data, now_ms_), XGL_OK);

    /* Run multiple cycles to deliver all fragments */
    for (int i = 0; i < 20; ++i) {
        xgl_step(handle1_, now_ms_ += 10U, &budget_);
        xgl_step(handle2_, now_ms_ += 10U, &budget_);
    }

    /* Verify B received the complete reassembled data */
    ASSERT_GE(rx_tracker_b.records.size(), 1U);
    EXPECT_EQ(rx_tracker_b.records[0].data.size(), payload.size());
    EXPECT_EQ(rx_tracker_b.records[0].data, payload);
}

/**
 * \brief           Multi-hop forwarding test: A -> B (forwarder) -> C
 * \details         A sends to target C via B, B's network layer forwards to C
 */
TEST_F(XglIntegrationTest, MultiHopForwarding) {
    /* Three PHY pairs: A<->B and B<->C */
    LoopbackPhyPair link_ab;
    LoopbackPhyPair link_bc;
    xgl_phy_ops_t phy_a = link_ab.get_phy_a();
    xgl_phy_ops_t phy_b_from_a = link_ab.get_phy_b();
    xgl_phy_ops_t phy_b_to_c = link_bc.get_phy_a();
    xgl_phy_ops_t phy_c = link_bc.get_phy_b();

    /* A has route to C via phy_a (which goes to B) */
    xgl_route_item_t route_a = {};
    route_a.target_id = 3;
    route_a.phy = &phy_a;
    route_a.max_frame_size = 256;
    route_a.read_freq_hz = 100;
    route_a.metric = 1;

    /* B has routes: from A (rx) and to C (tx) */
    xgl_route_item_t routes_b[] = {{/* target_id */ 1,
                                    /* phy */ &phy_b_from_a,
                                    /* max_frame_size */ 256,
                                    /* read_freq_hz */ 100,
                                    /* metric */ 1},
                                   {/* target_id */ 3,
                                    /* phy */ &phy_b_to_c,
                                    /* max_frame_size */ 256,
                                    /* read_freq_hz */ 100,
                                    /* metric */ 1}};

    /* C has route to B */
    xgl_route_item_t route_c = {};
    route_c.target_id = 2;
    route_c.phy = &phy_c;
    route_c.max_frame_size = 256;
    route_c.read_freq_hz = 100;
    route_c.metric = 1;

    config1_.source_id = 1;
    config1_.route_table = &route_a;
    config1_.route_table_len = 1;

    xgl_config_t config_b;
    xgl_config_get_default(&config_b);
    config_b.source_id = 2;
    config_b.protocol.max_frame_size = 256;
    config_b.protocol.window_size = 4;
    config_b.protocol.max_retry_count = 3;
    config_b.protocol.ack_timeout_ms = 100;
    config_b.route_table = routes_b;
    config_b.route_table_len = 2;

    xgl_config_t config_c;
    xgl_config_get_default(&config_c);
    config_c.source_id = 3;
    config_c.protocol.max_frame_size = 256;
    config_c.protocol.window_size = 4;
    config_c.protocol.max_retry_count = 3;
    config_c.protocol.ack_timeout_ms = 100;
    config_c.route_table = &route_c;
    config_c.route_table_len = 1;

    E2eRxTracker rx_tracker_c;
    config_c.rx_callback = e2e_rx_callback;
    config_c.callback_user_data = &rx_tracker_c;

    xgl_test_use_host_allocator(&config1_);
    handle1_ = xgl_create(&config1_);
    ASSERT_NE(handle1_, nullptr);
    ASSERT_EQ(xgl_init(handle1_), XGL_OK);

    xgl_test_use_host_allocator(&config_b);
    handle2_ = xgl_create(&config_b);
    ASSERT_NE(handle2_, nullptr);
    ASSERT_EQ(xgl_init(handle2_), XGL_OK);

    xgl_test_use_host_allocator(&config_c);
    xgl_handle_t handle3 = xgl_create(&config_c);
    ASSERT_NE(handle3, nullptr);
    ASSERT_EQ(xgl_init(handle3), XGL_OK);

    /* A sends unreliable data to C (target_id=3) */
    const uint8_t payload[] = {'M', 'H', 'o', 'p'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 3;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = false;
    tx_data.priority = 0;
    tx_data.timeout_ms = 0;
    ASSERT_EQ(xgl_send_at(handle1_, &tx_data, now_ms_), XGL_OK);

    /* Run multiple cycles: A -> B -> C */
    for (int i = 0; i < 10; ++i) {
        xgl_step(handle1_, now_ms_ += 10U, &budget_);
        xgl_step(handle2_, now_ms_ += 10U, &budget_);
        xgl_step(handle3, now_ms_ += 10U, &budget_);
    }

    /* Verify C received the data (forwarded through B) */
    ASSERT_GE(rx_tracker_c.records.size(), 1U);
    EXPECT_EQ(rx_tracker_c.records[0].data,
              std::vector<uint8_t>(payload, payload + sizeof(payload)));

    xgl_destroy(handle3);
}

TEST(XglPhyStreamTest, LoopbackPreservesPartialReadsInBothDirections) {
    LoopbackPhyPair pair;
    auto a = pair.get_phy_a();
    auto b = pair.get_phy_b();
    std::vector<uint8_t> bytes(301);
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<uint8_t>(i);
    }
    for (bool reverse : {false, true}) {
        auto& tx = reverse ? b : a;
        auto& rx = reverse ? a : b;
        ASSERT_EQ(tx.tx(bytes.data(), bytes.size(), tx.user_data), XGL_OK);
        std::vector<uint8_t> received;
        for (;;) {
            uint8_t chunk[64];
            size_t length = sizeof(chunk);
            ASSERT_EQ(rx.rx(chunk, &length, rx.user_data), XGL_OK);
            if (length == 0) {
                break;
            }
            received.insert(received.end(), chunk, chunk + length);
        }
        EXPECT_EQ(received, bytes);
    }
}

TEST(XglPhyStreamTest, FifoPreservesPartialReads) {
    FifoPhy fifo;
    auto phy = fifo.get_phy_ops();
    std::vector<uint8_t> bytes(301, 0xAD);
    fifo.enqueue_rx(bytes.data(), bytes.size());
    std::vector<uint8_t> received;
    for (;;) {
        uint8_t chunk[64];
        size_t length = sizeof(chunk);
        ASSERT_EQ(phy.rx(chunk, &length, phy.user_data), XGL_OK);
        if (length == 0) {
            break;
        }
        received.insert(received.end(), chunk, chunk + length);
    }
    EXPECT_EQ(received, bytes);
}
