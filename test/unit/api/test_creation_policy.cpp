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
#include <xgl/xgl_alloc.h>

TEST(CreationPolicyTest, MissingAllocatorFollowsCompiledPolicy) {
    xgl_config_t config;
    config = xgl_test_two_packet_config();
    xgl_handle_t handle = nullptr;
    const xgl_error_t error = xgl_init_allocated(&config, nullptr, &handle);
#if XGL_ALLOW_FALLBACK_MALLOC
    EXPECT_EQ(error, XGL_OK);
    EXPECT_NE(handle, nullptr);
#else
    EXPECT_EQ(error, XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(handle, nullptr);
#endif
    xgl_destroy(handle);
}

TEST(CreationPolicyTest, ExplicitAllocatorWorksWithoutHeapFallback) {
    xgl_config_t config;
    config = xgl_test_two_packet_config();
    xgl_handle_t handle = nullptr;
    ASSERT_EQ(xgl_init_allocated(&config, xgl_test_host_allocator(), &handle),
              XGL_OK);
    ASSERT_NE(handle, nullptr);
    const xgl_work_budget_t budget = {};
    EXPECT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);
    xgl_destroy(handle);
}
