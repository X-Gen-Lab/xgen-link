/**
 * \file            test_atomic.cpp
 * \brief           Atomic operations unit tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_atomic.h>

/*---------------------------------------------------------------------------*/
/* Basic Atomic Operations Tests                                             */
/*---------------------------------------------------------------------------*/

TEST(XglAtomicTest, InitAndLoad) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 42);
    EXPECT_EQ(xgl_atomic_load(&atomic), 42);
}

TEST(XglAtomicTest, Store) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 0);
    xgl_atomic_store(&atomic, 100);
    EXPECT_EQ(xgl_atomic_load(&atomic), 100);
}

TEST(XglAtomicTest, FetchInc) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 10);

    uint32_t old = xgl_atomic_fetch_inc(&atomic);
    EXPECT_EQ(old, 10);
    EXPECT_EQ(xgl_atomic_load(&atomic), 11);
}

TEST(XglAtomicTest, FetchDec) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 10);

    uint32_t old = xgl_atomic_fetch_dec(&atomic);
    EXPECT_EQ(old, 10);
    EXPECT_EQ(xgl_atomic_load(&atomic), 9);
}

TEST(XglAtomicTest, FetchAdd) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 10);

    uint32_t old = xgl_atomic_fetch_add(&atomic, 5);
    EXPECT_EQ(old, 10);
    EXPECT_EQ(xgl_atomic_load(&atomic), 15);
}

TEST(XglAtomicTest, FetchSub) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 20);

    uint32_t old = xgl_atomic_fetch_sub(&atomic, 7);
    EXPECT_EQ(old, 20);
    EXPECT_EQ(xgl_atomic_load(&atomic), 13);
}

TEST(XglAtomicTest, Exchange) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 100);

    uint32_t old = xgl_atomic_exchange(&atomic, 200);
    EXPECT_EQ(old, 100);
    EXPECT_EQ(xgl_atomic_load(&atomic), 200);
}

TEST(XglAtomicTest, CompareExchangeSuccess) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 50);

    uint32_t expected = 50;
    bool result = xgl_atomic_compare_exchange(&atomic, &expected, 75);

    EXPECT_TRUE(result);
    EXPECT_EQ(xgl_atomic_load(&atomic), 75);
    EXPECT_EQ(expected, 50);  /* Expected unchanged on success */
}

TEST(XglAtomicTest, CompareExchangeFailure) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 50);

    uint32_t expected = 40;  /* Wrong expected value */
    bool result = xgl_atomic_compare_exchange(&atomic, &expected, 75);

    EXPECT_FALSE(result);
    EXPECT_EQ(xgl_atomic_load(&atomic), 50);  /* Value unchanged */
    EXPECT_EQ(expected, 50);  /* Expected updated to actual value */
}

/*---------------------------------------------------------------------------*/
/* Convenience Macro Tests                                                   */
/*---------------------------------------------------------------------------*/

TEST(XglAtomicTest, IncMacro) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 10);

    uint32_t new_val = xgl_atomic_inc(&atomic);
    EXPECT_EQ(new_val, 11);
    EXPECT_EQ(xgl_atomic_load(&atomic), 11);
}

TEST(XglAtomicTest, DecMacro) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 10);

    uint32_t new_val = xgl_atomic_dec(&atomic);
    EXPECT_EQ(new_val, 9);
    EXPECT_EQ(xgl_atomic_load(&atomic), 9);
}

TEST(XglAtomicTest, AddMacro) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 10);

    uint32_t new_val = xgl_atomic_add(&atomic, 5);
    EXPECT_EQ(new_val, 15);
    EXPECT_EQ(xgl_atomic_load(&atomic), 15);
}

TEST(XglAtomicTest, SubMacro) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 20);

    uint32_t new_val = xgl_atomic_sub(&atomic, 7);
    EXPECT_EQ(new_val, 13);
    EXPECT_EQ(xgl_atomic_load(&atomic), 13);
}

/*---------------------------------------------------------------------------*/
/* Boolean Atomic Operations Tests                                           */
/*---------------------------------------------------------------------------*/

TEST(XglAtomicTest, BoolInitAndLoad) {
    xgl_atomic_bool_t atomic_bool;
    xgl_atomic_bool_init(&atomic_bool, true);
    EXPECT_TRUE(xgl_atomic_bool_load(&atomic_bool));

    xgl_atomic_bool_init(&atomic_bool, false);
    EXPECT_FALSE(xgl_atomic_bool_load(&atomic_bool));
}

TEST(XglAtomicTest, BoolStore) {
    xgl_atomic_bool_t atomic_bool;
    xgl_atomic_bool_init(&atomic_bool, false);

    xgl_atomic_bool_store(&atomic_bool, true);
    EXPECT_TRUE(xgl_atomic_bool_load(&atomic_bool));

    xgl_atomic_bool_store(&atomic_bool, false);
    EXPECT_FALSE(xgl_atomic_bool_load(&atomic_bool));
}

/*---------------------------------------------------------------------------*/
/* Edge Cases and Boundary Tests                                             */
/*---------------------------------------------------------------------------*/

TEST(XglAtomicTest, ZeroValue) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 0);
    EXPECT_EQ(xgl_atomic_load(&atomic), 0);

    xgl_atomic_fetch_inc(&atomic);
    EXPECT_EQ(xgl_atomic_load(&atomic), 1);
}

TEST(XglAtomicTest, MaxValue) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, UINT32_MAX);
    EXPECT_EQ(xgl_atomic_load(&atomic), UINT32_MAX);

    /* Test wraparound */
    xgl_atomic_fetch_inc(&atomic);
    EXPECT_EQ(xgl_atomic_load(&atomic), 0);
}

TEST(XglAtomicTest, MultipleOperations) {
    xgl_atomic_t atomic;
    xgl_atomic_init(&atomic, 100);

    xgl_atomic_fetch_add(&atomic, 50);   /* 150 */
    xgl_atomic_fetch_sub(&atomic, 30);   /* 120 */
    xgl_atomic_fetch_inc(&atomic);       /* 121 */
    xgl_atomic_fetch_dec(&atomic);       /* 120 */

    EXPECT_EQ(xgl_atomic_load(&atomic), 120);
}

/*---------------------------------------------------------------------------*/
/* Reference Counting Simulation                                             */
/*---------------------------------------------------------------------------*/

TEST(XglAtomicTest, ReferenceCountingSimulation) {
    xgl_atomic_t ref_count;
    xgl_atomic_init(&ref_count, 1);

    /* Simulate acquiring references */
    xgl_atomic_fetch_inc(&ref_count);  /* ref = 2 */
    xgl_atomic_fetch_inc(&ref_count);  /* ref = 3 */
    EXPECT_EQ(xgl_atomic_load(&ref_count), 3);

    /* Simulate releasing references */
    uint32_t old = xgl_atomic_fetch_dec(&ref_count);  /* ref = 2 */
    EXPECT_EQ(old, 3);

    old = xgl_atomic_fetch_dec(&ref_count);  /* ref = 1 */
    EXPECT_EQ(old, 2);

    old = xgl_atomic_fetch_dec(&ref_count);  /* ref = 0 */
    EXPECT_EQ(old, 1);

    /* Check if we should free (ref_count == 0) */
    EXPECT_EQ(xgl_atomic_load(&ref_count), 0);
}

/*---------------------------------------------------------------------------*/
/* Memory Fence Tests                                                        */
/*---------------------------------------------------------------------------*/

TEST(XglAtomicTest, MemoryFence) {
    /* Test that fence doesn't crash */
    xgl_atomic_fence();
    SUCCEED();
}

TEST(XglAtomicTest, CompilerBarrier) {
    /* Test that compiler barrier doesn't crash */
    xgl_atomic_compiler_barrier();
    SUCCEED();
}
