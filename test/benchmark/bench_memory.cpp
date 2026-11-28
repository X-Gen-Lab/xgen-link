/**
 * \file            bench_memory.cpp
 * \brief           Static workspace requirement and reported host memory
 * accounting
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-07-10
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */
#include "test_config.h"

#include <cstdio>
#include <vector>

#include "benchmark_support.h"

/**
 * \brief           Query exact static workspace and exercise a host workload
 * \details         Copy the workload configuration into persistent nodes. A
 *                  successful run completes every delivery.
 */
static bool run_memory_benchmark(const xgl_config_t& config) {
    BenchmarkPair pair;
    if (!pair.init(config)) {
        return false;
    }
    xgl_config_t static_config = pair.config_a;
    static_config.memory.allocator = nullptr;
    xgl_memory_requirements_t required = {};
    if (xgl_memory_requirements(&static_config, &required) != XGL_OK) {
        return false;
    }
    const size_t payload_size = config.protocol.max_frame_size - 40U;
    std::vector<uint8_t> payload(payload_size, 0xCD);
    for (int i = 0; i < 100; ++i) {
        if (!pair.exchange(payload.data(), payload.size(), false)) {
            return false;
        }
    }
    xgl_statistics_t statistics = {};
    if (xgl_stats_get(pair.a, &statistics) != XGL_OK) {
        return false;
    }
    std::printf(
        "%u,%u,%u,%zu,%zu,%zu,%zu,%llu\n",
        static_cast<unsigned>(config.protocol.max_frame_size),
        static_cast<unsigned>(config.protocol.window_size),
        static_cast<unsigned>(config.features.max_peers), payload_size,
        required.size, statistics.memory_used, statistics.memory_peak,
        static_cast<unsigned long long>(statistics.datalink.tx_packets));
    return true;
}

/** \brief           Report protocol storage separately from MCU
 * Flash/stack/driver RAM. */
int main() {
    const xgl_config_t workloads[] = {
        xgl_test_two_packet_config(),
        xgl_test_config(256, 288, 4, 2, 4, 4, 5),
        xgl_test_config(512, 544, 8, 8, 16, 16, 5),
        xgl_test_config(1024, 1056, 16, 16, 64, 64, 7),
    };
    std::printf("frame_bytes,window,peers,payload_bytes,static_workspace_bytes,"
                "reported_used,reported_peak,tx_packets\n");
    for (const auto& workload : workloads) {
        if (!run_memory_benchmark(workload)) {
            return 1;
        }
    }
    return 0;
}
