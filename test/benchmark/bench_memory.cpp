/**
 * \file            bench_memory.cpp
 * \brief           Static workspace requirement and reported host memory
 * accounting
 * \author          X-Gen Lab
 */
#include <cstdio>
#include <vector>

#include "benchmark_support.h"

/**
 * \brief           Query exact static workspace and exercise the selected host
 * preset
 * \param[in]       name: CSV preset label
 * \param[in]       config: Preset copied into persistent nodes
 * \return          True if initialization and all deliveries succeed
 */
static bool run_memory_benchmark(const char* name, const xgl_config_t& config) {
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
        "%s,%zu,%zu,%zu,%zu,%llu\n", name, payload_size, required.size,
        statistics.memory_used, statistics.memory_peak,
        static_cast<unsigned long long>(statistics.datalink.tx_packets));
    return true;
}

/** \brief           Report protocol storage separately from MCU
 * Flash/stack/driver RAM. */
int main() {
    const xgl_config_t tiny = XGL_CONFIG_PRESET_TINY;
    const xgl_config_t small = XGL_CONFIG_PRESET_SMALL;
    const xgl_config_t medium = XGL_CONFIG_PRESET_MEDIUM;
    const xgl_config_t large = XGL_CONFIG_PRESET_LARGE;
    std::printf("preset,payload_bytes,static_workspace_bytes,reported_used,"
                "reported_peak,tx_packets\n");
    return run_memory_benchmark("tiny", tiny) &&
                   run_memory_benchmark("small", small) &&
                   run_memory_benchmark("medium", medium) &&
                   run_memory_benchmark("large", large)
               ? 0
               : 1;
}
