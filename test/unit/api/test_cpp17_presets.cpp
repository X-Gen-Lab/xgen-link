/**
 * \file            test_cpp17_presets.cpp
 * \brief           Public preset initialization in standard C++17
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>

static constexpr xgl_config_t XGL_TEST_PRESET_TINY = XGL_CONFIG_PRESET_TINY;
static constexpr xgl_config_t XGL_TEST_PRESET_SMALL = XGL_CONFIG_PRESET_SMALL;
static constexpr xgl_config_t XGL_TEST_PRESET_MEDIUM = XGL_CONFIG_PRESET_MEDIUM;
static constexpr xgl_config_t XGL_TEST_PRESET_LARGE = XGL_CONFIG_PRESET_LARGE;
static constexpr xgl_config_t XGL_TEST_PRESET_PRODUCTION =
    XGL_CONFIG_PRESET_PRODUCTION;

static_assert(XGL_TEST_PRESET_TINY.protocol.max_frame_size == 128U);
static_assert(XGL_TEST_PRESET_SMALL.protocol.max_frame_size == 256U);
static_assert(XGL_TEST_PRESET_MEDIUM.protocol.max_frame_size == 512U);
static_assert(XGL_TEST_PRESET_LARGE.protocol.max_frame_size == 1024U);
static_assert(XGL_TEST_PRESET_PRODUCTION.auth_required);

TEST(Cpp17PresetTest, PresetsKeepStaticInitializationAndEmptyCallbacks) {
    for (const auto* preset : {&XGL_TEST_PRESET_TINY, &XGL_TEST_PRESET_SMALL,
                               &XGL_TEST_PRESET_MEDIUM, &XGL_TEST_PRESET_LARGE,
                               &XGL_TEST_PRESET_PRODUCTION}) {
        EXPECT_EQ(preset->memory.allocator, nullptr);
        EXPECT_EQ(preset->rx_callback, nullptr);
        EXPECT_EQ(preset->rx_accept_callback, nullptr);
        EXPECT_EQ(preset->error_callback, nullptr);
    }
}
