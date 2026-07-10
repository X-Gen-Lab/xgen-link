/**
 * \file            bench_memory.cpp
 * \brief           Memory usage benchmark for xgen-link protocol stack
 * \details         Measures actual pool usage for each config preset after
 *                  100 send cycles. Output: CSV to stdout for CI parsing.
 */

#include <xgl/xgl.h>
#include <mock_phy.h>
#include <cstdio>
#include <vector>
#include <cstring>

struct PresetConfig {
    const char* name;
    xgl_config_t config;
};

static void run_memory_benchmark(const PresetConfig& preset, int cycles) {
    /* Setup loopback PHY */
    LoopbackPhyPair phy_pair;
    xgl_phy_ops_t phy_a = phy_pair.get_phy_a();
    xgl_phy_ops_t phy_b = phy_pair.get_phy_b();

    xgl_config_t config_a = preset.config;
    config_a.source_id = 1;
    config_a.route_table_len = 0;  /* Will set after PHY setup */

    xgl_config_t config_b;
    xgl_config_get_default(&config_b);
    config_b.source_id = 2;

    xgl_route_item_t route_a = {
        .target_id = 2, .phy = &phy_a,
        .max_frame_size = preset.config.protocol.max_frame_size,
        .read_freq_hz = 1000, .metric = 1
    };
    xgl_route_item_t route_b = {
        .target_id = 1, .phy = &phy_b,
        .max_frame_size = config_b.protocol.max_frame_size,
        .read_freq_hz = 1000, .metric = 1
    };

    config_a.route_table = &route_a;
    config_a.route_table_len = 1;
    config_b.route_table = &route_b;
    config_b.route_table_len = 1;

    xgl_handle_t handle_a = xgl_create(&config_a);
    xgl_handle_t handle_b = xgl_create(&config_b);

    if (!handle_a || !handle_b || xgl_init(handle_a) != XGL_OK ||
        xgl_init(handle_b) != XGL_OK) {
        std::fprintf(stderr, "Init failed for preset=%s\n", preset.name);
        if (handle_a) xgl_destroy(handle_a);
        if (handle_b) xgl_destroy(handle_b);
        return;
    }

    /* Get initial stats */
    xgl_statistics_t stats_before, stats_after;
    xgl_stats_get(handle_a, &stats_before);

    /* Create payload sized to fit within max_frame_size minus headers */
    size_t payload_size = (preset.config.protocol.max_frame_size > 40)
        ? preset.config.protocol.max_frame_size - 40
        : 16;
    std::vector<uint8_t> payload(payload_size, 0xCD);

    /* Run send cycles */
    for (int i = 0; i < cycles; ++i) {
        xgl_tx_data_t tx_data = {};
        tx_data.target_id = 2;
        tx_data.data = payload.data();
        tx_data.data_len = payload.size();
        tx_data.reliable = false;
        tx_data.priority = 0;

        xgl_send(handle_a, &tx_data);

        xgl_run(handle_a, 1000);
        xgl_run(handle_b, 1000);
    }

    /* Get final stats */
    xgl_stats_get(handle_a, &stats_after);

    std::printf("%s,%d,%zu,%zu,%zu,%zu,%llu,%llu\n",
                preset.name,
                cycles,
                payload_size,
                stats_before.memory_used,
                stats_after.memory_used,
                stats_after.memory_peak,
                static_cast<unsigned long long>(stats_after.datalink.tx_packets),
                static_cast<unsigned long long>(stats_after.datalink.tx_bytes));

    xgl_destroy(handle_a);
    xgl_destroy(handle_b);
}

int main() {
    std::printf("preset,cycles,payload_bytes,mem_before,mem_after,mem_peak,tx_packets,tx_bytes\n");

    PresetConfig presets[] = {
        {"tiny",       {}},
        {"small",      {}},
        {"medium",     {}},
        {"large",      {}},
    };

    /* Initialize each preset with its defaults */
    xgl_config_get_default(&presets[0].config);
    presets[0].config = (xgl_config_t)XGL_CONFIG_PRESET_TINY;

    xgl_config_get_default(&presets[1].config);
    presets[1].config = (xgl_config_t)XGL_CONFIG_PRESET_SMALL;

    xgl_config_get_default(&presets[2].config);
    presets[2].config = (xgl_config_t)XGL_CONFIG_PRESET_MEDIUM;

    xgl_config_get_default(&presets[3].config);
    presets[3].config = (xgl_config_t)XGL_CONFIG_PRESET_LARGE;

    for (auto& p : presets) {
        run_memory_benchmark(p, 100);
    }

    return 0;
}
