/**
 * \file            test_mempool.cpp
 * \brief           Memory pool unit tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/internal/xgl_mempool.h>
#include <vector>
#include <cstring>

/*---------------------------------------------------------------------------*/
/* Test Fixtures                                                             */
/*---------------------------------------------------------------------------*/

class XglMempoolTest : public ::testing::Test {
protected:
    static constexpr size_t BUFFER_SIZE = 1024;
    static constexpr size_t BLOCK_SIZE = 64;

    uint8_t buffer[BUFFER_SIZE];
    xgl_mempool_t pool;

    void SetUp() override {
        memset(buffer, 0, sizeof(buffer));
        memset(&pool, 0, sizeof(pool));
    }

    void TearDown() override {
        xgl_mempool_destroy(&pool);
    }
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglMempoolTest, InitSuccess) {
    int result = xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(pool.block_size, BLOCK_SIZE);
    EXPECT_EQ(pool.block_count, BUFFER_SIZE / BLOCK_SIZE);
    EXPECT_EQ(pool.free_count, pool.block_count);
    EXPECT_EQ(pool.peak_used, 0);
    EXPECT_NE(pool.free_list, nullptr);
}

TEST_F(XglMempoolTest, InitNullPool) {
    int result = xgl_mempool_init(nullptr, buffer, BUFFER_SIZE, BLOCK_SIZE);
    EXPECT_EQ(result, -1);
}

TEST_F(XglMempoolTest, InitNullBuffer) {
    int result = xgl_mempool_init(&pool, nullptr, BUFFER_SIZE, BLOCK_SIZE);
    EXPECT_EQ(result, -1);
}

TEST_F(XglMempoolTest, InitZeroBufferSize) {
    int result = xgl_mempool_init(&pool, buffer, 0, BLOCK_SIZE);
    EXPECT_EQ(result, -1);
}

TEST_F(XglMempoolTest, InitZeroBlockSize) {
    int result = xgl_mempool_init(&pool, buffer, BUFFER_SIZE, 0);
    EXPECT_EQ(result, -1);
}

TEST_F(XglMempoolTest, InitBlockSizeTooSmall) {
    /* Block size must be at least pointer size */
    int result = xgl_mempool_init(&pool, buffer, BUFFER_SIZE, sizeof(void*) - 1);
    EXPECT_EQ(result, -1);
}

TEST_F(XglMempoolTest, InitBufferTooSmall) {
    /* Buffer too small for even one block */
    int result = xgl_mempool_init(&pool, buffer, BLOCK_SIZE - 1, BLOCK_SIZE);
    EXPECT_EQ(result, -1);
}

TEST_F(XglMempoolTest, Destroy) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);
    xgl_mempool_destroy(&pool);

    EXPECT_EQ(pool.pool, nullptr);
    EXPECT_EQ(pool.block_size, 0);
    EXPECT_EQ(pool.block_count, 0);
    EXPECT_EQ(pool.free_count, 0);
}

TEST_F(XglMempoolTest, DestroyNull) {
    /* Should not crash */
    xgl_mempool_destroy(nullptr);
}

/*---------------------------------------------------------------------------*/
/* Allocation Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglMempoolTest, AllocSingleBlock) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    void* block = xgl_mempool_alloc(&pool);

    EXPECT_NE(block, nullptr);
    EXPECT_EQ(pool.free_count, pool.block_count - 1);
    EXPECT_EQ(pool.peak_used, 1);
}

TEST_F(XglMempoolTest, AllocMultipleBlocks) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    size_t expected_blocks = BUFFER_SIZE / BLOCK_SIZE;
    std::vector<void*> blocks;

    for (size_t i = 0; i < expected_blocks; i++) {
        void* block = xgl_mempool_alloc(&pool);
        EXPECT_NE(block, nullptr);
        blocks.push_back(block);
    }

    EXPECT_EQ(blocks.size(), expected_blocks);
    EXPECT_EQ(pool.free_count, 0);
    EXPECT_EQ(pool.peak_used, expected_blocks);
}

TEST_F(XglMempoolTest, AllocAllBlocks) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    size_t expected_blocks = BUFFER_SIZE / BLOCK_SIZE;

    /* Allocate all blocks */
    for (size_t i = 0; i < expected_blocks; i++) {
        void* block = xgl_mempool_alloc(&pool);
        EXPECT_NE(block, nullptr);
    }

    /* Pool should be exhausted */
    EXPECT_TRUE(xgl_mempool_is_full(&pool));

    /* Next allocation should fail */
    void* block = xgl_mempool_alloc(&pool);
    EXPECT_EQ(block, nullptr);
}

TEST_F(XglMempoolTest, AllocNullPool) {
    void* block = xgl_mempool_alloc(nullptr);
    EXPECT_EQ(block, nullptr);
}

TEST_F(XglMempoolTest, AllocUniqueBlocks) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    size_t expected_blocks = BUFFER_SIZE / BLOCK_SIZE;
    std::vector<void*> blocks;

    /* Allocate all blocks */
    for (size_t i = 0; i < expected_blocks; i++) {
        void* block = xgl_mempool_alloc(&pool);
        ASSERT_NE(block, nullptr);
        blocks.push_back(block);
    }

    /* Verify all blocks are unique */
    for (size_t i = 0; i < blocks.size(); i++) {
        for (size_t j = i + 1; j < blocks.size(); j++) {
            EXPECT_NE(blocks[i], blocks[j]);
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Deallocation Tests                                                        */
/*---------------------------------------------------------------------------*/

TEST_F(XglMempoolTest, FreeSingleBlock) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    void* block = xgl_mempool_alloc(&pool);
    ASSERT_NE(block, nullptr);

    size_t free_before = pool.free_count;
    xgl_mempool_free(&pool, block);

    EXPECT_EQ(pool.free_count, free_before + 1);
}

TEST_F(XglMempoolTest, FreeMultipleBlocks) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    std::vector<void*> blocks;
    for (int i = 0; i < 5; i++) {
        void* block = xgl_mempool_alloc(&pool);
        ASSERT_NE(block, nullptr);
        blocks.push_back(block);
    }

    /* Free all blocks */
    for (void* block : blocks) {
        xgl_mempool_free(&pool, block);
    }

    EXPECT_EQ(pool.free_count, pool.block_count);
    EXPECT_TRUE(xgl_mempool_is_empty(&pool));
}

TEST_F(XglMempoolTest, FreeNullPool) {
    /* Should not crash */
    xgl_mempool_free(nullptr, buffer);
}

TEST_F(XglMempoolTest, FreeNullPointer) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    /* Should not crash */
    xgl_mempool_free(&pool, nullptr);
}

TEST_F(XglMempoolTest, FreeRejectsForeignAndMisalignedPointers) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    void* block = xgl_mempool_alloc(&pool);
    ASSERT_NE(block, nullptr);

    const size_t free_before = xgl_mempool_get_free_count(&pool);
    uint8_t foreign[BLOCK_SIZE] = {};
    xgl_mempool_free(&pool, foreign);
    xgl_mempool_free(&pool, static_cast<uint8_t*>(block) + 1);

    EXPECT_EQ(xgl_mempool_get_free_count(&pool), free_before);

    xgl_mempool_free(&pool, block);
}

TEST_F(XglMempoolTest, FreeRejectsDoubleFree) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    void* block = xgl_mempool_alloc(&pool);
    ASSERT_NE(block, nullptr);

    xgl_mempool_free(&pool, block);
    xgl_mempool_free(&pool, block);

    EXPECT_EQ(xgl_mempool_get_free_count(&pool), pool.block_count);

    std::vector<void*> blocks;
    for (size_t i = 0; i < pool.block_count; ++i) {
        void* allocated = xgl_mempool_alloc(&pool);
        ASSERT_NE(allocated, nullptr);
        for (void* previous : blocks) {
            EXPECT_NE(allocated, previous);
        }
        blocks.push_back(allocated);
    }
    EXPECT_EQ(xgl_mempool_alloc(&pool), nullptr);

    for (void* allocated : blocks) {
        xgl_mempool_free(&pool, allocated);
    }
}

TEST_F(XglMempoolTest, AllocFreeRealloc) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    /* Allocate a block */
    void* block1 = xgl_mempool_alloc(&pool);
    ASSERT_NE(block1, nullptr);

    /* Free it */
    xgl_mempool_free(&pool, block1);

    /* Allocate again - should get the same block */
    void* block2 = xgl_mempool_alloc(&pool);
    EXPECT_EQ(block1, block2);
}

/*---------------------------------------------------------------------------*/
/* Query Operation Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST_F(XglMempoolTest, GetFreeCount) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    size_t initial_free = xgl_mempool_get_free_count(&pool);
    EXPECT_EQ(initial_free, pool.block_count);

    xgl_mempool_alloc(&pool);
    EXPECT_EQ(xgl_mempool_get_free_count(&pool), initial_free - 1);
}

TEST_F(XglMempoolTest, GetUsedCount) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    EXPECT_EQ(xgl_mempool_get_used_count(&pool), 0);

    xgl_mempool_alloc(&pool);
    EXPECT_EQ(xgl_mempool_get_used_count(&pool), 1);

    xgl_mempool_alloc(&pool);
    EXPECT_EQ(xgl_mempool_get_used_count(&pool), 2);
}

TEST_F(XglMempoolTest, GetPeakUsed) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    EXPECT_EQ(xgl_mempool_get_peak_used(&pool), 0);

    void* block1 = xgl_mempool_alloc(&pool);
    EXPECT_EQ(xgl_mempool_get_peak_used(&pool), 1);

    void* block2 = xgl_mempool_alloc(&pool);
    EXPECT_EQ(xgl_mempool_get_peak_used(&pool), 2);

    void* block3 = xgl_mempool_alloc(&pool);
    EXPECT_EQ(xgl_mempool_get_peak_used(&pool), 3);

    /* Free one block - peak should remain */
    xgl_mempool_free(&pool, block2);
    EXPECT_EQ(xgl_mempool_get_peak_used(&pool), 3);

    /* Free all - peak should still remain */
    xgl_mempool_free(&pool, block1);
    xgl_mempool_free(&pool, block3);
    EXPECT_EQ(xgl_mempool_get_peak_used(&pool), 3);
}

TEST_F(XglMempoolTest, IsEmpty) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    EXPECT_TRUE(xgl_mempool_is_empty(&pool));

    void* block = xgl_mempool_alloc(&pool);
    EXPECT_FALSE(xgl_mempool_is_empty(&pool));

    xgl_mempool_free(&pool, block);
    EXPECT_TRUE(xgl_mempool_is_empty(&pool));
}

TEST_F(XglMempoolTest, IsFull) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    EXPECT_FALSE(xgl_mempool_is_full(&pool));

    /* Allocate all blocks */
    size_t expected_blocks = BUFFER_SIZE / BLOCK_SIZE;
    std::vector<void*> blocks;
    for (size_t i = 0; i < expected_blocks; i++) {
        void* block = xgl_mempool_alloc(&pool);
        ASSERT_NE(block, nullptr);
        blocks.push_back(block);
    }

    EXPECT_TRUE(xgl_mempool_is_full(&pool));

    /* Free one block */
    xgl_mempool_free(&pool, blocks[0]);
    EXPECT_FALSE(xgl_mempool_is_full(&pool));
}

TEST_F(XglMempoolTest, ResetStats) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    /* Allocate some blocks */
    void* block1 = xgl_mempool_alloc(&pool);
    void* block2 = xgl_mempool_alloc(&pool);
    void* block3 = xgl_mempool_alloc(&pool);

    EXPECT_EQ(xgl_mempool_get_peak_used(&pool), 3);

    /* Free one block */
    xgl_mempool_free(&pool, block2);
    EXPECT_EQ(xgl_mempool_get_used_count(&pool), 2);

    /* Reset stats - peak should be set to current usage */
    xgl_mempool_reset_stats(&pool);
    EXPECT_EQ(xgl_mempool_get_peak_used(&pool), 2);

    /* Cleanup */
    xgl_mempool_free(&pool, block1);
    xgl_mempool_free(&pool, block3);
}

/*---------------------------------------------------------------------------*/
/* Edge Case Tests                                                           */
/*---------------------------------------------------------------------------*/

TEST_F(XglMempoolTest, SmallBlockSize) {
    /* Test with minimum valid block size (pointer size) */
    int result = xgl_mempool_init(&pool, buffer, BUFFER_SIZE, sizeof(void*));
    EXPECT_EQ(result, 0);

    void* block = xgl_mempool_alloc(&pool);
    EXPECT_NE(block, nullptr);

    xgl_mempool_free(&pool, block);
}

TEST_F(XglMempoolTest, LargeBlockSize) {
    /* Test with block size equal to buffer size */
    int result = xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BUFFER_SIZE);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(pool.block_count, 1);

    void* block = xgl_mempool_alloc(&pool);
    EXPECT_NE(block, nullptr);
    EXPECT_TRUE(xgl_mempool_is_full(&pool));

    xgl_mempool_free(&pool, block);
    EXPECT_TRUE(xgl_mempool_is_empty(&pool));
}

TEST_F(XglMempoolTest, WriteToAllocatedBlock) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    void* block = xgl_mempool_alloc(&pool);
    ASSERT_NE(block, nullptr);

    /* Write pattern to block */
    uint8_t* data = static_cast<uint8_t*>(block);
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }

    /* Verify pattern */
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        EXPECT_EQ(data[i], static_cast<uint8_t>(i & 0xFF));
    }

    xgl_mempool_free(&pool, block);
}

/*---------------------------------------------------------------------------*/
/* Stress Tests                                                              */
/*---------------------------------------------------------------------------*/

TEST_F(XglMempoolTest, StressAllocFree) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    size_t expected_blocks = BUFFER_SIZE / BLOCK_SIZE;
    std::vector<void*> blocks;

    /* Perform many alloc/free cycles */
    for (int cycle = 0; cycle < 100; cycle++) {
        /* Allocate all blocks */
        blocks.clear();
        for (size_t i = 0; i < expected_blocks; i++) {
            void* block = xgl_mempool_alloc(&pool);
            ASSERT_NE(block, nullptr);
            blocks.push_back(block);
        }

        EXPECT_TRUE(xgl_mempool_is_full(&pool));

        /* Free all blocks */
        for (void* block : blocks) {
            xgl_mempool_free(&pool, block);
        }

        EXPECT_TRUE(xgl_mempool_is_empty(&pool));
    }
}

TEST_F(XglMempoolTest, StressRandomAllocFree) {
    xgl_mempool_init(&pool, buffer, BUFFER_SIZE, BLOCK_SIZE);

    std::vector<void*> allocated_blocks;

    /* Perform random allocations and deallocations */
    for (int i = 0; i < 1000; i++) {
        if (allocated_blocks.empty() || (rand() % 2 == 0 && !xgl_mempool_is_full(&pool))) {
            /* Allocate */
            void* block = xgl_mempool_alloc(&pool);
            if (block != nullptr) {
                allocated_blocks.push_back(block);
            }
        } else {
            /* Free random block */
            size_t idx = rand() % allocated_blocks.size();
            xgl_mempool_free(&pool, allocated_blocks[idx]);
            allocated_blocks.erase(allocated_blocks.begin() + idx);
        }
    }

    /* Cleanup */
    for (void* block : allocated_blocks) {
        xgl_mempool_free(&pool, block);
    }

    EXPECT_TRUE(xgl_mempool_is_empty(&pool));
}
