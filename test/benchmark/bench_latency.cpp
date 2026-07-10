/**
 * \file            bench_latency.cpp
 * \brief           Latency benchmark for xgen-link protocol stack
 * \details         Measures RTT for reliable send/ACK round-trip via loopback PHY.
 *                  Output: CSV to stdout for CI parsing.
 */

#include <xgl/xgl.h>
#include <mock_phy.h>
#include <chrono>
#include <cstdio>
#include <vector>

static void run_latency_benchmark(int iterations) {
    xgl_config_t config_a;
    xgl_config_get_default(&config_a);
    config_a.source_id = 1;
    config_a.protocol.max_frame_size = 256;
    config_a.protocol.window_size = 4;
    config_a.protocol.ack_timeout_ms = 100;
    config_a.features.enable_fragmentation = false;

    xgl_config_t config_b;
    xgl_config_get_default(&config_b);
    config_b.source_id = 2;
    config_b.protocol.max_frame_size = 256;
    config_b.protocol.window_size = 4;
    config_b.protocol.ack_timeout_ms = 100;
    config_b.features.enable_fragmentation = false;

    LoopbackPhyPair phy_pair;
    xgl_phy_ops_t phy_a = phy_pair.get_phy_a();
    xgl_phy_ops_t phy_b = phy_pair.get_phy_b();

    xgl_route_item_t route_a = {
        .target_id = 2, .phy = &phy_a,
        .max_frame_size = 256, .read_freq_hz = 1000, .metric = 1
    };
    xgl_route_item_t route_b = {
        .target_id = 1, .phy = &phy_b,
        .max_frame_size = 256, .read_freq_hz = 1000, .metric = 1
    };

    config_a.route_table = &route_a;
    config_a.route_table_len = 1;
    config_b.route_table = &route_b;
    config_b.route_table_len = 1;

    xgl_handle_t handle_a = xgl_create(&config_a);
    xgl_handle_t handle_b = xgl_create(&config_b);
    if (!handle_a || !handle_b || xgl_init(handle_a) != XGL_OK ||
        xgl_init(handle_b) != XGL_OK) {
        std::fprintf(stderr, "Init failed\n");
        if (handle_a) xgl_destroy(handle_a);
        if (handle_b) xgl_destroy(handle_b);
        return;
    }

    const uint8_t payload[] = "latency-test";
    std::vector<int64_t> latencies_us;
    latencies_us.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();

        xgl_tx_data_t tx_data = {};
        tx_data.target_id = 2;
        tx_data.data = payload;
        tx_data.data_len = sizeof(payload) - 1;
        tx_data.reliable = true;
        tx_data.priority = 0;

        xgl_send(handle_a, &tx_data);

        /* Process through loopback: A->B and B->A (ACK) */
        for (int cycle = 0; cycle < 5; ++cycle) {
            xgl_run(handle_a, 1000);
            xgl_run(handle_b, 1000);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count();
        latencies_us.push_back(elapsed_us);
    }

    /* Compute statistics */
    int64_t min_us = latencies_us[0], max_us = latencies_us[0], sum_us = 0;
    for (auto v : latencies_us) {
        if (v < min_us) min_us = v;
        if (v > max_us) max_us = v;
        sum_us += v;
    }
    double avg_us = static_cast<double>(sum_us) / iterations;

    /* Also output per-iteration data */
    for (int i = 0; i < iterations; ++i) {
        std::printf("%d,%lld\n", i, static_cast<long long>(latencies_us[i]));
    }

    std::fprintf(stderr, "Latency summary (us): min=%lld, avg=%.0f, max=%lld\n",
                 static_cast<long long>(min_us), avg_us,
                 static_cast<long long>(max_us));

    xgl_destroy(handle_a);
    xgl_destroy(handle_b);
}

int main() {
    std::printf("iteration,latency_us\n");
    run_latency_benchmark(100);
    return 0;
}
