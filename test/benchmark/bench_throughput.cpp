/**
 * \file            bench_throughput.cpp
 * \brief           Host CPU throughput for completed simulated application
 * deliveries
 * \author          X-Gen Lab
 */
#include <chrono>
#include <cstdio>
#include <vector>

#include "benchmark_support.h"

/**
 * \brief           Measure complete unreliable exchanges with an explicit
 * logical clock
 * \param[in]       payload_size: Application payload bytes
 * \param[in]       iterations: Number of delivered packets
 * \return          True when every exchange completed
 */
static bool run_throughput_benchmark(size_t payload_size, int iterations) {
    xgl_config_t config = XGL_CONFIG_PRESET_TINY;
    config.protocol.max_frame_size = 1200U;
    config.memory.rx_buffer_size = 1200U;
    config.protocol.window_size = 1U;
    BenchmarkPair pair;
    if (!pair.init(config)) {
        return false;
    }
    std::vector<uint8_t> payload(payload_size, 0xAB);
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        if (!pair.exchange(payload.data(), payload.size(), false)) {
            return false;
        }
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    const double packets_per_second =
        elapsed > 0 ? static_cast<double>(iterations) * 1000000.0 / elapsed
                    : 0.0;
    std::printf("%zu,%d,%lld,%.0f,%.1f\n", payload_size, iterations,
                static_cast<long long>(elapsed), packets_per_second,
                packets_per_second * payload_size * 8.0 / 1000.0);
    return true;
}

/** \brief           Run selected host sizes; this is not a board or PHY
 * bandwidth claim. */
int main() {
    std::printf("payload_bytes,deliveries,host_elapsed_us,packets_per_sec,"
                "throughput_kbps\n");
    return run_throughput_benchmark(64U, 1000) &&
                   run_throughput_benchmark(256U, 1000) &&
                   run_throughput_benchmark(1024U, 500)
               ? 0
               : 1;
}
