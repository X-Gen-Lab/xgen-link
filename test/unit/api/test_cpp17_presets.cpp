/**
 * \file            test_cpp17_presets.cpp
 * \brief           Public preset initialization in standard C++17
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>

static constexpr xgl_config_t tiny = XGL_CONFIG_PRESET_TINY;
static constexpr xgl_config_t small = XGL_CONFIG_PRESET_SMALL;
static constexpr xgl_config_t medium = XGL_CONFIG_PRESET_MEDIUM;
static constexpr xgl_config_t large = XGL_CONFIG_PRESET_LARGE;
static constexpr xgl_config_t production = XGL_CONFIG_PRESET_PRODUCTION;

static_assert(tiny.protocol.max_frame_size == 128U);
static_assert(small.protocol.max_frame_size == 256U);
static_assert(medium.protocol.max_frame_size == 512U);
static_assert(large.protocol.max_frame_size == 1024U);
static_assert(production.auth_required);

TEST(Cpp17PresetTest, PresetsKeepStaticInitializationAndEmptyCallbacks) {
    for (const auto* preset : {&tiny, &small, &medium, &large, &production}) {
        EXPECT_EQ(preset->memory.allocator, nullptr);
        EXPECT_EQ(preset->rx_callback, nullptr);
        EXPECT_EQ(preset->rx_accept_callback, nullptr);
        EXPECT_EQ(preset->error_callback, nullptr);
    }
}
