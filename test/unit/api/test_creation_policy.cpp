/**
 * \file            test_creation_policy.cpp
 * \brief           Explicit allocation and configured heap fallback contracts
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */
#include "test_config.h"
#include "test_host_allocator.h"
#include <gtest/gtest.h>
#include <xgl/xgl.h>

TEST(CreationPolicyTest, MissingAllocatorFollowsCompiledPolicy) {
    xgl_config_t config;
    config = xgl_test_two_packet_config();
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
    config = xgl_test_two_packet_config();
    config.memory.allocator = xgl_test_host_allocator();
    const xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(xgl_init(handle), XGL_OK);
    xgl_destroy(handle);
}
