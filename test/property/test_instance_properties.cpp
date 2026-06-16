/**
 * \file            test_instance_properties.cpp
 * \brief           Instance management property tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include "property_framework.h"
#include <xgl/xgl.h>
#include <vector>
#include <cstring>

/* Feature: x-gen-link, Property 6: Instance Isolation */
TEST(XglInstanceProperties, InstanceIsolation) {
    PropertyTestGenerator gen;

    /* Run property test with multiple iterations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Create two independent instances with different configurations */

        /* Setup PHY operations for both instances using lambda functions */
        xgl_phy_ops_t phy1 = {
            .tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
                (void)data; (void)len; (void)user_data;
                return XGL_OK;  /* Simulate successful transmission */
            },
            .rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
                (void)buffer; (void)user_data;
                *len = 0;  /* No data available */
                return XGL_ERR_TIMEOUT;
            },
            .user_data = nullptr
        };

        xgl_phy_ops_t phy2 = {
            .tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
                (void)data; (void)len; (void)user_data;
                return XGL_OK;  /* Simulate successful transmission */
            },
            .rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
                (void)buffer; (void)user_data;
                *len = 0;  /* No data available */
                return XGL_ERR_TIMEOUT;
            },
            .user_data = nullptr
        };

        /* Setup route tables for both instances */
        xgl_route_item_t route1 = {
            .target_id = 2,
            .phy = &phy1,
            .max_frame_size = 256,
            .read_freq_hz = 100,
            .metric = 0
        };

        xgl_route_item_t route2 = {
            .target_id = 3,
            .phy = &phy2,
            .max_frame_size = 512,
            .read_freq_hz = 100,
            .metric = 0
        };

        /* Create configuration for instance 1 */
        xgl_config_t config1;
        xgl_config_get_default(&config1);
        config1.name = "instance1";
        config1.source_id = 1;
        config1.memory.tx_pool_size = 1024 + (gen.random_uint16() % 1024);
        config1.protocol.max_retry_count = 3 + (gen.random_uint8() % 5);
        config1.protocol.window_size = 2 + (gen.random_uint8() % 6);
        config1.protocol.max_frame_size = 128 + (gen.random_uint16() % 128);
        config1.memory.rx_buffer_size = config1.protocol.max_frame_size + XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE;
        config1.route_table = &route1;
        config1.route_table_len = 1;

        /* Create configuration for instance 2 with DIFFERENT values */
        xgl_config_t config2;
        xgl_config_get_default(&config2);
        config2.name = "instance2";
        config2.source_id = 10;  /* Different source ID */
        config2.memory.tx_pool_size = 2048 + (gen.random_uint16() % 2048);  /* Different pool size */
        config2.protocol.max_retry_count = 5 + (gen.random_uint8() % 3);  /* Different retry count */
        config2.protocol.window_size = 4 + (gen.random_uint8() % 4);  /* Different window size */
        config2.protocol.max_frame_size = 256 + (gen.random_uint16() % 256);  /* Different frame size */
        config2.memory.rx_buffer_size = config2.protocol.max_frame_size + XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE;
        config2.route_table = &route2;
        config2.route_table_len = 1;

        /* Create both instances */
        xgl_handle_t handle1 = xgl_create(&config1);
        xgl_handle_t handle2 = xgl_create(&config2);

        ASSERT_NE(handle1, nullptr) << "Failed to create instance 1 in iteration " << iteration;
        ASSERT_NE(handle2, nullptr) << "Failed to create instance 2 in iteration " << iteration;
        ASSERT_NE(handle1, handle2) << "Instances should have different handles";

        /* Initialize both instances */
        xgl_error_t err1 = xgl_init(handle1);
        xgl_error_t err2 = xgl_init(handle2);

        ASSERT_EQ(err1, XGL_OK) << "Failed to initialize instance 1 in iteration " << iteration
                                << ": " << xgl_error_string(err1);
        ASSERT_EQ(err2, XGL_OK) << "Failed to initialize instance 2 in iteration " << iteration
                                << ": " << xgl_error_string(err2);

        /* Get initial statistics for both instances */
        xgl_statistics_t stats1_before, stats2_before;
        err1 = xgl_stats_get(handle1, &stats1_before);
        err2 = xgl_stats_get(handle2, &stats2_before);

        ASSERT_EQ(err1, XGL_OK) << "Failed to get stats for instance 1";
        ASSERT_EQ(err2, XGL_OK) << "Failed to get stats for instance 2";

        /* Perform operations on instance 1 ONLY */
        std::vector<uint8_t> test_data1 = gen.random_bytes(32);

        for (int i = 0; i < 5; ++i) {
            xgl_tx_data_t tx_data = {
                .target_id = 2,  /* Route configured for instance 1 */
                .data_type = gen.random_uint8(),
                .data = test_data1.data(),
                .data_len = test_data1.size(),
                .reliable = true,
                .priority = static_cast<uint8_t>(gen.random_uint8() % 8),
                .timeout_ms = 0
            };

            xgl_error_t send_err = xgl_send(handle1, &tx_data);
            /* May succeed or fail due to resource constraints, both are acceptable */
            (void)send_err;
        }

        /* Get statistics after operations on instance 1 */
        xgl_statistics_t stats1_after, stats2_after;
        err1 = xgl_stats_get(handle1, &stats1_after);
        err2 = xgl_stats_get(handle2, &stats2_after);

        ASSERT_EQ(err1, XGL_OK) << "Failed to get stats for instance 1 after operations";
        ASSERT_EQ(err2, XGL_OK) << "Failed to get stats for instance 2 after operations";

        /* CRITICAL PROPERTY: Instance 2 should be completely unaffected by operations on instance 1 */

        /* Verify instance 2 statistics are unchanged */
        EXPECT_EQ(stats2_after.datalink.tx_packets, stats2_before.datalink.tx_packets)
            << "Instance 2 TX packets changed in iteration " << iteration
            << " after operations on instance 1 (isolation violated)";

        EXPECT_EQ(stats2_after.datalink.tx_bytes, stats2_before.datalink.tx_bytes)
            << "Instance 2 TX bytes changed in iteration " << iteration
            << " after operations on instance 1 (isolation violated)";

        EXPECT_EQ(stats2_after.datalink.rx_packets, stats2_before.datalink.rx_packets)
            << "Instance 2 RX packets changed in iteration " << iteration
            << " after operations on instance 1 (isolation violated)";

        EXPECT_EQ(stats2_after.datalink.rx_bytes, stats2_before.datalink.rx_bytes)
            << "Instance 2 RX bytes changed in iteration " << iteration
            << " after operations on instance 1 (isolation violated)";

        EXPECT_EQ(stats2_after.datalink.tx_errors, stats2_before.datalink.tx_errors)
            << "Instance 2 TX errors changed in iteration " << iteration
            << " after operations on instance 1 (isolation violated)";

        EXPECT_EQ(stats2_after.datalink.rx_errors, stats2_before.datalink.rx_errors)
            << "Instance 2 RX errors changed in iteration " << iteration
            << " after operations on instance 1 (isolation violated)";

        /* Verify instance 1 statistics DID change (sanity check) */
        /* At least one of these should have changed if operations were performed */
        bool instance1_changed = (stats1_after.datalink.tx_packets != stats1_before.datalink.tx_packets) ||
                                (stats1_after.datalink.tx_bytes != stats1_before.datalink.tx_bytes) ||
                                (stats1_after.datalink.tx_errors != stats1_before.datalink.tx_errors);

        EXPECT_TRUE(instance1_changed)
            << "Instance 1 statistics did not change after operations in iteration " << iteration
            << " (sanity check failed - operations may not have been performed)";

        /* Now perform operations on instance 2 ONLY */
        std::vector<uint8_t> test_data2 = gen.random_bytes(48);

        /* Save instance 1 stats before operating on instance 2 */
        xgl_statistics_t stats1_before_op2;
        err1 = xgl_stats_get(handle1, &stats1_before_op2);
        ASSERT_EQ(err1, XGL_OK);

        for (int i = 0; i < 7; ++i) {
            xgl_tx_data_t tx_data = {
                .target_id = 3,  /* Route configured for instance 2 */
                .data_type = gen.random_uint8(),
                .data = test_data2.data(),
                .data_len = test_data2.size(),
                .reliable = true,
                .priority = static_cast<uint8_t>(gen.random_uint8() % 8),
                .timeout_ms = 0
            };

            xgl_error_t send_err = xgl_send(handle2, &tx_data);
            (void)send_err;
        }

        /* Get statistics after operations on instance 2 */
        xgl_statistics_t stats1_after_op2, stats2_after_op2;
        err1 = xgl_stats_get(handle1, &stats1_after_op2);
        err2 = xgl_stats_get(handle2, &stats2_after_op2);

        ASSERT_EQ(err1, XGL_OK);
        ASSERT_EQ(err2, XGL_OK);

        /* CRITICAL PROPERTY: Instance 1 should be unaffected by operations on instance 2 */

        EXPECT_EQ(stats1_after_op2.datalink.tx_packets, stats1_before_op2.datalink.tx_packets)
            << "Instance 1 TX packets changed in iteration " << iteration
            << " after operations on instance 2 (isolation violated)";

        EXPECT_EQ(stats1_after_op2.datalink.tx_bytes, stats1_before_op2.datalink.tx_bytes)
            << "Instance 1 TX bytes changed in iteration " << iteration
            << " after operations on instance 2 (isolation violated)";

        EXPECT_EQ(stats1_after_op2.datalink.rx_packets, stats1_before_op2.datalink.rx_packets)
            << "Instance 1 RX packets changed in iteration " << iteration
            << " after operations on instance 2 (isolation violated)";

        EXPECT_EQ(stats1_after_op2.datalink.rx_bytes, stats1_before_op2.datalink.rx_bytes)
            << "Instance 1 RX bytes changed in iteration " << iteration
            << " after operations on instance 2 (isolation violated)";

        /* Verify instance 2 statistics DID change (sanity check) */
        bool instance2_changed = (stats2_after_op2.datalink.tx_packets != stats2_after.datalink.tx_packets) ||
                                (stats2_after_op2.datalink.tx_bytes != stats2_after.datalink.tx_bytes) ||
                                (stats2_after_op2.datalink.tx_errors != stats2_after.datalink.tx_errors);

        EXPECT_TRUE(instance2_changed)
            << "Instance 2 statistics did not change after operations in iteration " << iteration
            << " (sanity check failed)";

        /* Verify both instances remain functional after cross-operations */
        xgl_statistics_t stats1_final, stats2_final;
        err1 = xgl_stats_get(handle1, &stats1_final);
        err2 = xgl_stats_get(handle2, &stats2_final);

        EXPECT_EQ(err1, XGL_OK)
            << "Instance 1 corrupted after operations in iteration " << iteration;
        EXPECT_EQ(err2, XGL_OK)
            << "Instance 2 corrupted after operations in iteration " << iteration;

        /* Test that we can still reset statistics independently */
        err1 = xgl_stats_reset(handle1);
        ASSERT_EQ(err1, XGL_OK) << "Failed to reset stats for instance 1";

        /* Get stats after reset */
        xgl_statistics_t stats1_reset, stats2_after_reset;
        err1 = xgl_stats_get(handle1, &stats1_reset);
        err2 = xgl_stats_get(handle2, &stats2_after_reset);

        ASSERT_EQ(err1, XGL_OK);
        ASSERT_EQ(err2, XGL_OK);

        /* Verify instance 1 stats were reset */
        EXPECT_EQ(stats1_reset.datalink.tx_packets, 0)
            << "Instance 1 stats not reset in iteration " << iteration;
        EXPECT_EQ(stats1_reset.datalink.tx_bytes, 0)
            << "Instance 1 stats not reset in iteration " << iteration;

        /* Verify instance 2 stats were NOT affected by instance 1 reset */
        EXPECT_EQ(stats2_after_reset.datalink.tx_packets, stats2_final.datalink.tx_packets)
            << "Instance 2 stats changed after instance 1 reset in iteration " << iteration
            << " (isolation violated)";
        EXPECT_EQ(stats2_after_reset.datalink.tx_bytes, stats2_final.datalink.tx_bytes)
            << "Instance 2 stats changed after instance 1 reset in iteration " << iteration
            << " (isolation violated)";

        /* Clean up both instances */
        xgl_destroy(handle1);
        xgl_destroy(handle2);
    }
}
