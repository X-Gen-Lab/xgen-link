/**
 * \file            bench_latency.cpp
 * \brief           Host CPU latency for reliable simulated exchanges
 * \author          X-Gen Lab
 */
#include <chrono>
#include <cstdio>

#include "benchmark_support.h"

/** \brief           Measure completed deliveries and ACK processing, with
 * failures visible. */
int main() {
    xgl_config_t config = XGL_CONFIG_PRESET_TINY;
    config.protocol.window_size = 1U;
    BenchmarkPair pair;
    if (!pair.init(config)) {
        return 1;
    }
    const uint8_t payload[] = "latency-test";
    std::printf("iteration,host_elapsed_us\n");
    for (int i = 0; i < 100; ++i) {
        auto start = std::chrono::steady_clock::now();
        if (!pair.exchange(payload, sizeof(payload) - 1U, true)) {
            return 2;
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
        std::printf("%d,%lld\n", i, static_cast<long long>(elapsed));
    }
    return 0;
}
