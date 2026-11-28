/**
 * \file            test_cpp17_config.cpp
 * \brief           Application configuration initialization in standard C++17
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>

constexpr xgl_config_t application_config() {
    xgl_config_t config{};
    config.source_id = 2U;
    config.memory.rx_buffer_size = 128U;
    config.protocol = {200U, 3U, 1U, 64U};
    config.features.max_peers = 1U;
    config.features.max_tx_packets = 1U;
    return config;
}

TEST(Cpp17ConfigTest, ValidatesStaticallyInitializedApplicationConfig) {
    static constexpr auto config = application_config();
    static_assert(config.protocol.max_frame_size == 64U);
    static_assert(config.rx_callback == nullptr);
    EXPECT_EQ(xgl_config_validate(&config), XGL_OK);
}
