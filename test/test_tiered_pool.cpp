/**
 * \file            test_tiered_pool.cpp
 * \brief           Unit tests for tiered memory pool
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <cstring>
#include "xgl/internal/xgl_tiered_pool.h"

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglTieredPoolTest : public ::testing::Test {
protected:
    xgl_tiered_pool_t pool;
    
    void SetUp() override {
        memset(&pool, 0, sizeof(pool));
    }
    
    void TearDown() override {
        xgl_tiered_pool_destroy(&pool);
    }
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglTieredPoolTest, InitSuccess) {
    int result = xgl_tiered_pool_init(&pool, 10, 5, 2);
    EXPECT_EQ(result, 0);
    EXPECT_NE(pool.small_buffer, nullptr);
    EXPECT_NE(pool.medium_buffer, nullptr);
    EXPECT_NE(pool.large_buffer, nullptr);
}

TEST_F(XglTieredPoolTest, InitNullPool) {
    int result = xgl_tiered_pool_init(nullptr, 10, 5, 2);
    EXPECT_EQ(result, -1);
}

TEST_F(XglTieredPoolTest, InitZeroCounts) {
    int result = xgl_tiered_pool_init(&pool, 0, 0, 0);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(pool.small_buffer, nullptr);
    EXPECT_EQ(pool.medium_buffer, nullptr);
    EXPECT_EQ(pool.large_buffer, nullptr);
}

TEST_F(XglTieredPoolTest, InitOnlySmall) {
    int result = xgl_tiered_pool_init(&pool, 10, 0, 0);
    EXPECT_EQ(result, 0);
    EXPECT_NE(pool.small_buffer, nullptr);
    EXPECT_EQ(pool.medium_buffer, nullptr);
    EXPECT_EQ(pool.large_buffer, nullptr);
}

TEST_F(XglTieredPoolTest, InitOnlyMedium) {
    int result = xgl_tiered_pool_init(&pool, 0, 5, 0);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(pool.small_buffer, nullptr);
    EXPECT_NE(pool.medium_buffer, nullptr);
    EXPECT_EQ(pool.large_buffer, nullptr);
}

TEST_F(XglTieredPoolTest, InitOnlyLarge) {
    int result = xgl_tiered_pool_init(&pool, 0, 0, 2);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(pool.small_buffer, nullptr);
    EXPECT_EQ(pool.medium_buffer, nullptr);
    EXPECT_NE(pool.large_buffer, nullptr);
}

TEST_F(XglTieredPoolTest, InitStaticUsesApplicationBuffers) {
    uint8_t small_buffer[2 * XGL_TIERED_POOL_SMALL_SIZE] = {};
    uint8_t medium_buffer[1 * XGL_TIERED_POOL_MEDIUM_SIZE] = {};
    uint8_t large_buffer[1 * XGL_TIERED_POOL_LARGE_SIZE] = {};

    int result = xgl_tiered_pool_init_static(&pool,
                                             small_buffer, 2,
                                             medium_buffer, 1,
                                             large_buffer, 1);
    ASSERT_EQ(result, 0);
    EXPECT_EQ(pool.small_buffer, small_buffer);
    EXPECT_EQ(pool.medium_buffer, medium_buffer);
    EXPECT_EQ(pool.large_buffer, large_buffer);

    void* small = xgl_tiered_pool_alloc(&pool, 32);
    void* medium = xgl_tiered_pool_alloc(&pool, 128);
    void* large = xgl_tiered_pool_alloc(&pool, 512);
    ASSERT_NE(small, nullptr);
    ASSERT_NE(medium, nullptr);
    ASSERT_NE(large, nullptr);

    xgl_tiered_pool_free(&pool, small, 32);
    xgl_tiered_pool_free(&pool, medium, 128);
    xgl_tiered_pool_free(&pool, large, 512);
}

TEST_F(XglTieredPoolTest, InitStaticRejectsMissingRequiredBuffer) {
    uint8_t small_buffer[2 * XGL_TIERED_POOL_SMALL_SIZE] = {};

    EXPECT_EQ(xgl_tiered_pool_init_static(&pool,
                                          nullptr, 1,
                                          nullptr, 0,
                                          nullptr, 0), -1);

    EXPECT_EQ(xgl_tiered_pool_init_static(&pool,
                                          small_buffer, 2,
                                          nullptr, 1,
                                          nullptr, 0), -1);
}

TEST_F(XglTieredPoolTest, Destroy) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    xgl_tiered_pool_destroy(&pool);
    EXPECT_EQ(pool.small_buffer, nullptr);
    EXPECT_EQ(pool.medium_buffer, nullptr);
    EXPECT_EQ(pool.large_buffer, nullptr);
}

TEST_F(XglTieredPoolTest, DestroyNull) {
    xgl_tiered_pool_destroy(nullptr);  /* Should not crash */
}

/*---------------------------------------------------------------------------*/
/* Allocation Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglTieredPoolTest, AllocSmall) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, 32);
    EXPECT_NE(ptr, nullptr);
    xgl_tiered_pool_free(&pool, ptr, 32);
}

TEST_F(XglTieredPoolTest, AllocMedium) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, 128);
    EXPECT_NE(ptr, nullptr);
    xgl_tiered_pool_free(&pool, ptr, 128);
}

TEST_F(XglTieredPoolTest, AllocLarge) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, 512);
    EXPECT_NE(ptr, nullptr);
    xgl_tiered_pool_free(&pool, ptr, 512);
}

TEST_F(XglTieredPoolTest, AllocExactSmallSize) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, XGL_TIERED_POOL_SMALL_SIZE);
    EXPECT_NE(ptr, nullptr);
    xgl_tiered_pool_free(&pool, ptr, XGL_TIERED_POOL_SMALL_SIZE);
}

TEST_F(XglTieredPoolTest, AllocExactMediumSize) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, XGL_TIERED_POOL_MEDIUM_SIZE);
    EXPECT_NE(ptr, nullptr);
    xgl_tiered_pool_free(&pool, ptr, XGL_TIERED_POOL_MEDIUM_SIZE);
}

TEST_F(XglTieredPoolTest, AllocExactLargeSize) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, XGL_TIERED_POOL_LARGE_SIZE);
    EXPECT_NE(ptr, nullptr);
    xgl_tiered_pool_free(&pool, ptr, XGL_TIERED_POOL_LARGE_SIZE);
}

TEST_F(XglTieredPoolTest, AllocTooLarge) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, XGL_TIERED_POOL_LARGE_SIZE + 1);
    EXPECT_EQ(ptr, nullptr);
}

TEST_F(XglTieredPoolTest, AllocZeroSize) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, 0);
    EXPECT_EQ(ptr, nullptr);
}

TEST_F(XglTieredPoolTest, AllocNullPool) {
    void* ptr = xgl_tiered_pool_alloc(nullptr, 32);
    EXPECT_EQ(ptr, nullptr);
}

TEST_F(XglTieredPoolTest, AllocMultipleSmall) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = xgl_tiered_pool_alloc(&pool, 32);
        EXPECT_NE(ptrs[i], nullptr);
    }
    for (int i = 0; i < 10; i++) {
        xgl_tiered_pool_free(&pool, ptrs[i], 32);
    }
}

TEST_F(XglTieredPoolTest, AllocExhaustSmall) {
    xgl_tiered_pool_init(&pool, 2, 0, 0);
    void* ptr1 = xgl_tiered_pool_alloc(&pool, 32);
    void* ptr2 = xgl_tiered_pool_alloc(&pool, 32);
    void* ptr3 = xgl_tiered_pool_alloc(&pool, 32);
    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_EQ(ptr3, nullptr);  /* Pool exhausted */
    xgl_tiered_pool_free(&pool, ptr1, 32);
    xgl_tiered_pool_free(&pool, ptr2, 32);
}

TEST_F(XglTieredPoolTest, FreesFallbackAllocationToOwningPool) {
    ASSERT_EQ(xgl_tiered_pool_init(&pool, 1, 1, 0), 0);

    void* small = xgl_tiered_pool_alloc(&pool, 32);
    void* fallback_medium = xgl_tiered_pool_alloc(&pool, 32);
    ASSERT_NE(small, nullptr);
    ASSERT_NE(fallback_medium, nullptr);

    EXPECT_EQ(xgl_mempool_get_used_count(&pool.small_pool), 1U);
    EXPECT_EQ(xgl_mempool_get_used_count(&pool.medium_pool), 1U);

    xgl_tiered_pool_free(&pool, fallback_medium, 32);

    EXPECT_EQ(xgl_mempool_get_used_count(&pool.small_pool), 1U);
    EXPECT_EQ(xgl_mempool_get_used_count(&pool.medium_pool), 0U);

    xgl_tiered_pool_free(&pool, small, 32);
}

TEST_F(XglTieredPoolTest, SmartAllocationSelectsSmallest) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    
    /* Request 32 bytes - should use small pool */
    void* ptr1 = xgl_tiered_pool_alloc(&pool, 32);
    EXPECT_NE(ptr1, nullptr);
    size_t used_small = xgl_tiered_pool_get_used_memory(&pool);
    EXPECT_EQ(used_small, XGL_TIERED_POOL_SMALL_SIZE);
    xgl_tiered_pool_free(&pool, ptr1, 32);
    
    /* Request 128 bytes - should use medium pool */
    void* ptr2 = xgl_tiered_pool_alloc(&pool, 128);
    EXPECT_NE(ptr2, nullptr);
    size_t used_medium = xgl_tiered_pool_get_used_memory(&pool);
    EXPECT_EQ(used_medium, XGL_TIERED_POOL_MEDIUM_SIZE);
    xgl_tiered_pool_free(&pool, ptr2, 128);
    
    /* Request 512 bytes - should use large pool */
    void* ptr3 = xgl_tiered_pool_alloc(&pool, 512);
    EXPECT_NE(ptr3, nullptr);
    size_t used_large = xgl_tiered_pool_get_used_memory(&pool);
    EXPECT_EQ(used_large, XGL_TIERED_POOL_LARGE_SIZE);
    xgl_tiered_pool_free(&pool, ptr3, 512);
}

/*---------------------------------------------------------------------------*/
/* Free Tests                                                                */
/*---------------------------------------------------------------------------*/

TEST_F(XglTieredPoolTest, FreeNullPool) {
    void* ptr = malloc(32);
    xgl_tiered_pool_free(nullptr, ptr, 32);  /* Should not crash */
    free(ptr);
}

TEST_F(XglTieredPoolTest, FreeNullPointer) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    xgl_tiered_pool_free(&pool, nullptr, 32);  /* Should not crash */
}

TEST_F(XglTieredPoolTest, FreeZeroSize) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, 32);
    xgl_tiered_pool_free(&pool, ptr, 0);  /* Should not crash */
    xgl_tiered_pool_free(&pool, ptr, 32);  /* Proper free */
}

TEST_F(XglTieredPoolTest, AllocFreeRealloc) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr1 = xgl_tiered_pool_alloc(&pool, 32);
    EXPECT_NE(ptr1, nullptr);
    xgl_tiered_pool_free(&pool, ptr1, 32);
    void* ptr2 = xgl_tiered_pool_alloc(&pool, 32);
    EXPECT_NE(ptr2, nullptr);
    xgl_tiered_pool_free(&pool, ptr2, 32);
}

/*---------------------------------------------------------------------------*/
/* Query Tests                                                               */
/*---------------------------------------------------------------------------*/

TEST_F(XglTieredPoolTest, GetFreeMemory) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    size_t expected = 10 * XGL_TIERED_POOL_SMALL_SIZE +
                     5 * XGL_TIERED_POOL_MEDIUM_SIZE +
                     2 * XGL_TIERED_POOL_LARGE_SIZE;
    EXPECT_EQ(xgl_tiered_pool_get_free_memory(&pool), expected);
}

TEST_F(XglTieredPoolTest, GetUsedMemory) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    EXPECT_EQ(xgl_tiered_pool_get_used_memory(&pool), 0);
    
    void* ptr = xgl_tiered_pool_alloc(&pool, 32);
    EXPECT_EQ(xgl_tiered_pool_get_used_memory(&pool),
              XGL_TIERED_POOL_SMALL_SIZE);
    
    xgl_tiered_pool_free(&pool, ptr, 32);
    EXPECT_EQ(xgl_tiered_pool_get_used_memory(&pool), 0);
}

TEST_F(XglTieredPoolTest, GetPeakMemory) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    EXPECT_EQ(xgl_tiered_pool_get_peak_memory(&pool), 0);
    
    void* ptr1 = xgl_tiered_pool_alloc(&pool, 32);
    void* ptr2 = xgl_tiered_pool_alloc(&pool, 128);
    size_t peak = XGL_TIERED_POOL_SMALL_SIZE + XGL_TIERED_POOL_MEDIUM_SIZE;
    EXPECT_EQ(xgl_tiered_pool_get_peak_memory(&pool), peak);
    
    xgl_tiered_pool_free(&pool, ptr1, 32);
    EXPECT_EQ(xgl_tiered_pool_get_peak_memory(&pool), peak);
    
    xgl_tiered_pool_free(&pool, ptr2, 128);
    EXPECT_EQ(xgl_tiered_pool_get_peak_memory(&pool), peak);
}

TEST_F(XglTieredPoolTest, ResetStats) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    void* ptr = xgl_tiered_pool_alloc(&pool, 32);
    xgl_tiered_pool_free(&pool, ptr, 32);
    
    EXPECT_GT(xgl_tiered_pool_get_peak_memory(&pool), 0);
    
    xgl_tiered_pool_reset_stats(&pool);
    EXPECT_EQ(xgl_tiered_pool_get_peak_memory(&pool), 0);
}

TEST_F(XglTieredPoolTest, GetFreeMemoryNull) {
    EXPECT_EQ(xgl_tiered_pool_get_free_memory(nullptr), 0);
}

TEST_F(XglTieredPoolTest, GetUsedMemoryNull) {
    EXPECT_EQ(xgl_tiered_pool_get_used_memory(nullptr), 0);
}

TEST_F(XglTieredPoolTest, GetPeakMemoryNull) {
    EXPECT_EQ(xgl_tiered_pool_get_peak_memory(nullptr), 0);
}

TEST_F(XglTieredPoolTest, ResetStatsNull) {
    xgl_tiered_pool_reset_stats(nullptr);  /* Should not crash */
}

/*---------------------------------------------------------------------------*/
/* Data Integrity Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglTieredPoolTest, WriteToAllocatedMemory) {
    xgl_tiered_pool_init(&pool, 10, 5, 2);
    
    /* Test small block */
    uint8_t* small = (uint8_t*)xgl_tiered_pool_alloc(&pool, 32);
    ASSERT_NE(small, nullptr);
    memset(small, 0xAA, 32);
    for (int i = 0; i < 32; i++) {
        EXPECT_EQ(small[i], 0xAA);
    }
    xgl_tiered_pool_free(&pool, small, 32);
    
    /* Test medium block */
    uint8_t* medium = (uint8_t*)xgl_tiered_pool_alloc(&pool, 128);
    ASSERT_NE(medium, nullptr);
    memset(medium, 0xBB, 128);
    for (int i = 0; i < 128; i++) {
        EXPECT_EQ(medium[i], 0xBB);
    }
    xgl_tiered_pool_free(&pool, medium, 128);
    
    /* Test large block */
    uint8_t* large = (uint8_t*)xgl_tiered_pool_alloc(&pool, 512);
    ASSERT_NE(large, nullptr);
    memset(large, 0xCC, 512);
    for (int i = 0; i < 512; i++) {
        EXPECT_EQ(large[i], 0xCC);
    }
    xgl_tiered_pool_free(&pool, large, 512);
}

/*---------------------------------------------------------------------------*/
/* Stress Tests                                                              */
/*---------------------------------------------------------------------------*/

TEST_F(XglTieredPoolTest, StressMixedAllocations) {
    xgl_tiered_pool_init(&pool, 20, 10, 5);
    
    void* ptrs[35];
    size_t sizes[35];
    
    /* Allocate mixed sizes */
    for (int i = 0; i < 35; i++) {
        if (i < 20) {
            sizes[i] = 32;
        } else if (i < 30) {
            sizes[i] = 128;
        } else {
            sizes[i] = 512;
        }
        ptrs[i] = xgl_tiered_pool_alloc(&pool, sizes[i]);
        EXPECT_NE(ptrs[i], nullptr);
    }
    
    /* Free all */
    for (int i = 0; i < 35; i++) {
        xgl_tiered_pool_free(&pool, ptrs[i], sizes[i]);
    }
    
    /* Verify all memory is free */
    size_t expected_free = 20 * XGL_TIERED_POOL_SMALL_SIZE +
                          10 * XGL_TIERED_POOL_MEDIUM_SIZE +
                          5 * XGL_TIERED_POOL_LARGE_SIZE;
    EXPECT_EQ(xgl_tiered_pool_get_free_memory(&pool), expected_free);
}
