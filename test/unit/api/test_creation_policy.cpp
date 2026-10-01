/**
 * \file            test_creation_policy.cpp
 * \brief           Explicit allocation and configured heap fallback contracts
 * \author          X-Gen Lab
 */
#include <gtest/gtest.h>
#include <xgen/memory/libc_allocator.h>
#include <xgl/xgl.h>

TEST(CreationPolicyTest, MissingAllocatorFollowsCompiledPolicy) {
    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    ASSERT_EQ(config.memory.allocator, nullptr);
    const xgl_handle_t handle = xgl_create(&config);
#if XGL_ALLOW_FALLBACK_MALLOC
    EXPECT_NE(handle, nullptr);
#else
    EXPECT_EQ(handle, nullptr);
#endif
    xgl_destroy(handle);
}

TEST(CreationPolicyTest, ExplicitAllocatorWorksWithoutHeapFallback) {
    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    config.memory.allocator = xgm_allocator_libc();
    const xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(xgl_init(handle), XGL_OK);
    xgl_destroy(handle);
}
