/**
 * \file            bench_throughput.cpp
 * \brief           Throughput benchmark for xgen-link protocol stack
 * \details         Measures packets/sec for unreliable sends via loopback PHY
 *                  at various payload sizes (64B, 256B, 1KB).
 *                  Output: CSV to stdout for CI parsing.
 */

#include <xgl/xgl.h>
#include <mock_phy.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

static void run_throughput_benchmark(size_t payload_size, int iterations) {
    xgl_config_t config_a;
    xgl_config_get_default(&config_a);
    config_a.source_id = 1;
    config_a.protocol.max_frame_size = 1200;
    config_a.memory.rx_buffer_size = 1200;
    config_a.protocol.window_size = 16;
    config_a.features.enable_fragmentation = (payload_size > 200);

    xgl_config_t config_b;
    xgl_config_get_default(&config_b);
    config_b.source_id = 2;
    config_b.protocol.max_frame_size = 1200;
    config_b.memory.rx_buffer_size = 1200;
    config_b.protocol.window_size = 16;
    config_b.features.enable_fragmentation = (payload_size > 200);

    LoopbackPhyPair phy_pair;
    xgl_phy_ops_t phy_a = phy_pair.get_phy_a();
    xgl_phy_ops_t phy_b = phy_pair.get_phy_b();

    xgl_route_item_t route_a = {
        .target_id = 2, .phy = &phy_a,
        .max_frame_size = 1200, .read_freq_hz = 1000, .metric = 1
    };
    xgl_route_item_t route_b = {
        .target_id = 1, .phy = &phy_b,
        .max_frame_size = 1200, .read_freq_hz = 1000, .metric = 1
    };

    config_a.route_table = &route_a;
    config_a.route_table_len = 1;
    config_b.route_table = &route_b;
    config_b.route_table_len = 1;

    xgl_handle_t handle_a = xgl_create(&config_a);
    xgl_handle_t handle_b = xgl_create(&config_b);
    if (!handle_a || !handle_b) {
        std::fprintf(stderr, "Create failed for payload_size=%zu\n", payload_size);
        if (handle_a) xgl_destroy(handle_a);
        if (handle_b) xgl_destroy(handle_b);
        return;
    }

    xgl_error_t err_a = xgl_init(handle_a);
    xgl_error_t err_b = xgl_init(handle_b);
    if (err_a != XGL_OK || err_b != XGL_OK) {
        std::fprintf(stderr, "Init failed for payload_size=%zu (err_a=%d, err_b=%d)\n",
                     payload_size, (int)err_a, (int)err_b);
        xgl_destroy(handle_a);
        xgl_destroy(handle_b);
        return;
    }

    /* Create payload */
    std::vector<uint8_t> payload(payload_size, 0xAB);

    /* Warm-up: run a few cycles to establish peer state */
    for (int i = 0; i < 3; ++i) {
        xgl_run(handle_a, 1000);
        xgl_run(handle_b, 1000);
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        xgl_tx_data_t tx_data = {};
        tx_data.target_id = 2;
        tx_data.data = payload.data();
        tx_data.data_len = payload.size();
        tx_data.reliable = false;
        tx_data.priority = 0;

        xgl_send(handle_a, &tx_data);

        /* Process through loopback */
        xgl_run(handle_a, 1000);
        xgl_run(handle_b, 1000);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end - start).count();

    double packets_per_sec = (elapsed_us > 0)
        ? (static_cast<double>(iterations) * 1000000.0 / elapsed_us)
        : 0.0;
    double throughput_kbps = (packets_per_sec * payload_size * 8.0) / 1000.0;

    std::printf("%zu,%d,%lld,%.0f,%.1f\n",
                payload_size, iterations,
                static_cast<long long>(elapsed_us),
                packets_per_sec, throughput_kbps);

    xgl_destroy(handle_a);
    xgl_destroy(handle_b);
}

int main() {
    std::printf("payload_bytes,iterations,elapsed_us,packets_per_sec,throughput_kbps\n");

    run_throughput_benchmark(64, 1000);
    run_throughput_benchmark(256, 1000);
    run_throughput_benchmark(1024, 500);

    return 0;
}
