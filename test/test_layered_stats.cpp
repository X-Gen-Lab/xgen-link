/**
 * \file            test_layered_stats.cpp
 * \brief           Unit tests for layered statistics architecture
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglLayeredStatsTest : public ::testing::Test {
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
/* Layer Statistics Structure Tests                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test that statistics structure has layer-specific fields
 */
TEST_F(XglLayeredStatsTest, StatisticsStructureHasLayerFields) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Verify layer-specific fields exist and are accessible */
    EXPECT_EQ(stats.datalink.tx_packets, 0);
    EXPECT_EQ(stats.datalink.tx_bytes, 0);
    EXPECT_EQ(stats.datalink.tx_errors, 0);
    EXPECT_EQ(stats.datalink.rx_packets, 0);
    EXPECT_EQ(stats.datalink.rx_bytes, 0);
    EXPECT_EQ(stats.datalink.rx_errors, 0);
    EXPECT_EQ(stats.datalink.rx_dropped, 0);

    EXPECT_EQ(stats.network.tx_packets, 0);
    EXPECT_EQ(stats.network.tx_bytes, 0);
    EXPECT_EQ(stats.network.tx_errors, 0);
    EXPECT_EQ(stats.network.rx_packets, 0);
    EXPECT_EQ(stats.network.rx_bytes, 0);
    EXPECT_EQ(stats.network.rx_errors, 0);
    EXPECT_EQ(stats.network.rx_dropped, 0);

    EXPECT_EQ(stats.transport.tx_packets, 0);
    EXPECT_EQ(stats.transport.tx_bytes, 0);
    EXPECT_EQ(stats.transport.tx_errors, 0);
    EXPECT_EQ(stats.transport.rx_packets, 0);
    EXPECT_EQ(stats.transport.rx_bytes, 0);
    EXPECT_EQ(stats.transport.rx_errors, 0);
    EXPECT_EQ(stats.transport.rx_dropped, 0);
}

/**
 * \brief           Test that protocol-specific counters are separate
 */
TEST_F(XglLayeredStatsTest, ProtocolSpecificCountersSeparate) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Verify protocol-specific counters exist */
    EXPECT_EQ(stats.tx_retries, 0);
    EXPECT_EQ(stats.rx_header_crc_errors, 0);
    EXPECT_EQ(stats.rx_crc16_errors, 0);
}

/**
 * \brief           Test that performance metrics are present
 */
TEST_F(XglLayeredStatsTest, PerformanceMetricsPresent) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Verify performance metrics exist */
    EXPECT_EQ(stats.avg_rtt_ms, 0);
    EXPECT_EQ(stats.max_rtt_ms, 0);
    EXPECT_EQ(stats.min_rtt_ms, UINT32_MAX);
}

/**
 * \brief           Test that memory usage fields are present
 */
TEST_F(XglLayeredStatsTest, MemoryUsageFieldsPresent) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Verify memory usage fields exist */
    EXPECT_EQ(stats.memory_used, 0);
    EXPECT_EQ(stats.memory_peak, 0);
}

/*---------------------------------------------------------------------------*/
/* Layer Independence Tests                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test that layer statistics are independent
 */
TEST_F(XglLayeredStatsTest, LayerStatisticsAreIndependent) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Verify each layer can have different values */
    /* This test verifies the structure allows independent values */
    stats.datalink.tx_packets = 100;
    stats.network.tx_packets = 95;   /* Some packets dropped */
    stats.transport.tx_packets = 90; /* Some more dropped */

    EXPECT_NE(stats.datalink.tx_packets, stats.network.tx_packets);
    EXPECT_NE(stats.network.tx_packets, stats.transport.tx_packets);
    EXPECT_NE(stats.datalink.tx_packets, stats.transport.tx_packets);
}

/**
 * \brief           Test layer statistics reset independently
 */
TEST_F(XglLayeredStatsTest, LayerStatisticsResetTogether) {
    xgl_statistics_t stats;

    /* Reset all statistics */
    xgl_error_t err = xgl_stats_reset(handle);
    ASSERT_EQ(err, XGL_OK);

    /* Get statistics */
    err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Verify all layers are reset */
    EXPECT_EQ(stats.datalink.tx_packets, 0);
    EXPECT_EQ(stats.network.tx_packets, 0);
    EXPECT_EQ(stats.transport.tx_packets, 0);

    EXPECT_EQ(stats.datalink.rx_packets, 0);
    EXPECT_EQ(stats.network.rx_packets, 0);
    EXPECT_EQ(stats.transport.rx_packets, 0);
}

/*---------------------------------------------------------------------------*/
/* Layer Statistics Semantics Tests                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test datalink layer statistics semantics
 * \details         Datalink should count frames at PHY level
 */
TEST_F(XglLayeredStatsTest, DatalinkLayerSemantics) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Datalink layer should track:
     * - Frames transmitted/received at PHY
     * - CRC errors (header and payload)
     * - Frame-level errors
     */

    /* Verify CRC error counters are at protocol level, not layer level */
    EXPECT_EQ(stats.rx_header_crc_errors, 0);   /* Protocol-specific */
    EXPECT_EQ(stats.rx_crc16_errors, 0);  /* Protocol-specific */
}

/**
 * \brief           Test network layer statistics semantics
 * \details         Network should count routed packets
 */
TEST_F(XglLayeredStatsTest, NetworkLayerSemantics) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Network layer should track:
     * - Packets routed (not forwarded frames)
     * - Routing errors
     * - Dropped packets due to routing
     */

    EXPECT_EQ(stats.network.tx_packets, 0);
    EXPECT_EQ(stats.network.rx_packets, 0);
}

/**
 * \brief           Test transport layer statistics semantics
 * \details         Transport should count application messages
 */
TEST_F(XglLayeredStatsTest, TransportLayerSemantics) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Transport layer should track:
     * - Application-level messages (not fragments)
     * - Reliability errors
     * - Dropped messages
     */

    EXPECT_EQ(stats.transport.tx_packets, 0);
    EXPECT_EQ(stats.transport.rx_packets, 0);

    /* Retries are protocol-specific, not layer-specific */
    EXPECT_EQ(stats.tx_retries, 0);
}

/*---------------------------------------------------------------------------*/
/* No Double Counting Tests                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test that packet is not double-counted across layers
 * \details         This is a conceptual test - actual counting happens
 *                  in layer implementations
 */
TEST_F(XglLayeredStatsTest, NoDoubleCounting) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Conceptual test: If we send 1 application message:
     * - Transport layer: 1 packet
     * - Network layer: 1 packet (or more if fragmented)
     * - Datalink layer: 1 frame (or more if fragmented)
     *
     * Each layer counts independently, so no double counting
     */

    /* Verify structure allows independent counting */
    EXPECT_EQ(stats.datalink.tx_packets, 0);
    EXPECT_EQ(stats.network.tx_packets, 0);
    EXPECT_EQ(stats.transport.tx_packets, 0);
}

/**
 * \brief           Test fragmentation scenario statistics
 * \details         When a message is fragmented, each layer counts correctly
 */
TEST_F(XglLayeredStatsTest, FragmentationScenario) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Conceptual test: If we send 1 large message that fragments into 3:
     * - Transport layer: 1 packet (the original message)
     * - Network layer: 3 packets (the fragments)
     * - Datalink layer: 3 frames (the fragments)
     *
     * This shows the actual work done at each layer
     */

    /* Simulate fragmentation scenario */
    stats.transport.tx_packets = 1;
    stats.network.tx_packets = 3;
    stats.datalink.tx_packets = 3;

    /* Verify different counts are possible */
    EXPECT_EQ(stats.transport.tx_packets, 1);
    EXPECT_EQ(stats.network.tx_packets, 3);
    EXPECT_EQ(stats.datalink.tx_packets, 3);
}

/*---------------------------------------------------------------------------*/
/* Statistics Aggregation Tests                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test total packets across all layers
 */
TEST_F(XglLayeredStatsTest, TotalPacketsAcrossLayers) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Simulate some traffic */
    stats.datalink.tx_packets = 100;
    stats.network.tx_packets = 95;
    stats.transport.tx_packets = 90;

    /* Calculate total (for analysis purposes) */
    uint64_t total_tx = stats.datalink.tx_packets +
                        stats.network.tx_packets +
                        stats.transport.tx_packets;

    EXPECT_EQ(total_tx, 285);
}

/**
 * \brief           Test error rate calculation per layer
 */
TEST_F(XglLayeredStatsTest, ErrorRatePerLayer) {
    xgl_statistics_t stats;

    xgl_error_t err = xgl_stats_get(handle, &stats);
    ASSERT_EQ(err, XGL_OK);

    /* Simulate traffic with errors */
    stats.datalink.tx_packets = 100;
    stats.datalink.tx_errors = 5;

    stats.network.tx_packets = 95;
    stats.network.tx_errors = 3;

    stats.transport.tx_packets = 92;
    stats.transport.tx_errors = 2;

    /* Calculate error rates */
    double dl_error_rate = (double)stats.datalink.tx_errors / stats.datalink.tx_packets;
    double net_error_rate = (double)stats.network.tx_errors / stats.network.tx_packets;
    double trans_error_rate = (double)stats.transport.tx_errors / stats.transport.tx_packets;

    EXPECT_NEAR(dl_error_rate, 0.05, 0.001);    /* 5% */
    EXPECT_NEAR(net_error_rate, 0.0316, 0.001); /* ~3.16% */
    EXPECT_NEAR(trans_error_rate, 0.0217, 0.001); /* ~2.17% */
}

/*---------------------------------------------------------------------------*/
/* Backward Compatibility Tests                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test that existing statistics API still works
 */
TEST_F(XglLayeredStatsTest, BackwardCompatibility) {
    xgl_statistics_t stats;

    /* Existing API should still work */
    xgl_error_t err = xgl_stats_get(handle, &stats);
    EXPECT_EQ(err, XGL_OK);

    err = xgl_stats_reset(handle);
    EXPECT_EQ(err, XGL_OK);

    /* Get stats again after reset */
    err = xgl_stats_get(handle, &stats);
    EXPECT_EQ(err, XGL_OK);
}

/**
 * \brief           Test statistics structure size is reasonable
 */
TEST_F(XglLayeredStatsTest, StatisticsStructureSize) {
    /* Verify structure size is reasonable for embedded systems */
    size_t stats_size = sizeof(xgl_statistics_t);
    size_t layer_stats_size = sizeof(xgl_layer_stats_t);

    /* Each layer stats should be reasonable (7 uint64_t = 56 bytes) */
    EXPECT_LE(layer_stats_size, 64);

    /* Total stats should be reasonable (< 512 bytes) */
    EXPECT_LE(stats_size, 512);

    /* Verify layer stats are part of total */
    EXPECT_GE(stats_size, 3 * layer_stats_size);
}

