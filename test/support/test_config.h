/**
 * \file            test_config.h
 * \brief           Application configuration for host test workloads
 * \author          X-Gen Lab
 */
#ifndef XGL_TEST_CONFIG_H
#define XGL_TEST_CONFIG_H

#include <xgl/xgl.h>

constexpr xgl_config_t xgl_test_config(uint16_t frame, size_t rx_bytes,
                                       uint8_t window, uint16_t peers,
                                       size_t tx_slots, size_t rx_slots,
                                       uint8_t retries) {
    xgl_config_t config{};
    config.source_id = 1U;
    config.memory.rx_buffer_size = rx_bytes;
    config.protocol = {1000U, retries, window, frame};
    config.features.max_peers = peers;
    config.features.max_tx_packets = tx_slots;
    config.features.max_rx_buffered_packets = rx_slots;
    config.features.peer_idle_timeout_ms = 60000U;
    return config;
}

constexpr xgl_config_t xgl_test_single_packet_config() {
    return xgl_test_config(128, 128, 1, 1, 1, 0, 3);
}

constexpr xgl_config_t xgl_test_two_packet_config() {
    return xgl_test_config(128, 160, 2, 1, 2, 2, 3);
}

constexpr xgl_config_t xgl_test_config() {
#if XGL_BUILD_PROFILE_BOOT
    return xgl_test_config(
        XGL_DATALINK_MAX_FRAME_SIZE < 128U ? XGL_DATALINK_MAX_FRAME_SIZE : 128U,
        128U, 1U, 1U, 1U, 0U, 3U);
#else
    return xgl_test_config(
        XGL_DATALINK_MAX_FRAME_SIZE < 512U ? XGL_DATALINK_MAX_FRAME_SIZE : 512U,
        544U, XGL_BUILD_WINDOW_CAPACITY < 8U ? XGL_BUILD_WINDOW_CAPACITY : 8U,
        8U, 16U, 16U, 5U);
#endif
}

inline xgl_error_t xgl_test_measure_config(const xgl_config_t* config) {
    xgl_memory_requirements_t requirements{};
    return xgl_memory_requirements(config, &requirements);
}

#endif /* XGL_TEST_CONFIG_H */
