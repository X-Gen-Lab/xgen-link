/**
 * \file            test_allocator.cpp
 * \brief           Custom allocator unit tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/internal/xgl_allocator.h>
#include <cstring>

#ifndef XGL_ALLOW_FALLBACK_MALLOC
#error "XGL_ALLOW_FALLBACK_MALLOC must declare the build allocator fallback policy"
#endif

/*---------------------------------------------------------------------------*/
/* Test Fixtures                                                             */
/*---------------------------------------------------------------------------*/

class XglAllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Reset any global state if needed */
    }

    void TearDown() override {
        /* Clean up */
    }
};

/*---------------------------------------------------------------------------*/
/* Default Allocator Tests                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test getting default allocator
 */
TEST_F(XglAllocatorTest, GetDefaultAllocator) {
    xgl_allocator_t* allocator = xgl_allocator_get_default();

    ASSERT_NE(allocator, nullptr);
    ASSERT_NE(allocator->malloc, nullptr);
    ASSERT_NE(allocator->free, nullptr);
}

/**
 * \brief           Test default allocator malloc/free
 */
TEST_F(XglAllocatorTest, DefaultAllocatorMallocFree) {
    xgl_allocator_t* allocator = xgl_allocator_get_default();

    /* Allocate memory */
    void* ptr = allocator->malloc(128);
    ASSERT_NE(ptr, nullptr);

    /* Write to memory */
    memset(ptr, 0xAA, 128);

    /* Free memory */
    allocator->free(ptr);
}

/*---------------------------------------------------------------------------*/
/* Allocator Wrapper Tests                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_alloc with NULL allocator (uses default)
 */
TEST_F(XglAllocatorTest, AllocWithNullAllocator) {
    void* ptr = xgl_alloc(nullptr, 256);
#if XGL_ALLOW_FALLBACK_MALLOC
    ASSERT_NE(ptr, nullptr);

    /* Write to memory */
    memset(ptr, 0xBB, 256);

    /* Free memory */
    xgl_free(nullptr, ptr);
#else
    EXPECT_EQ(ptr, nullptr);
#endif
}

/**
 * \brief           Test xgl_alloc with zero size
 */
TEST_F(XglAllocatorTest, AllocZeroSize) {
    void* ptr = xgl_alloc(nullptr, 0);
    EXPECT_EQ(ptr, nullptr);
}

/**
 * \brief           Test xgl_free with NULL pointer
 */
TEST_F(XglAllocatorTest, FreeNullPointer) {
    /* Should not crash */
    xgl_free(nullptr, nullptr);
}

/**
 * \brief           Test xgl_alloc with custom allocator
 */
TEST_F(XglAllocatorTest, AllocWithCustomAllocator) {
    xgl_allocator_t* allocator = xgl_allocator_get_default();

    void* ptr = xgl_alloc(allocator, 512);
    ASSERT_NE(ptr, nullptr);

    /* Write to memory */
    memset(ptr, 0xCC, 512);

    /* Free memory */
    xgl_free(allocator, ptr);
}

/*---------------------------------------------------------------------------*/
/* Custom Allocator Tests                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Custom allocator for testing
 */
class TestAllocator {
public:
    size_t alloc_count = 0;
    size_t free_count = 0;
    size_t total_allocated = 0;

    static void* test_malloc(size_t size) {
        TestAllocator* self = get_instance();
        self->alloc_count++;
        self->total_allocated += size;
        return malloc(size);
    }

    static void test_free(void* ptr) {
        TestAllocator* self = get_instance();
        self->free_count++;
        free(ptr);
    }

    static TestAllocator* get_instance() {
        static TestAllocator instance;
        return &instance;
    }

    void reset() {
        alloc_count = 0;
        free_count = 0;
        total_allocated = 0;
    }
};

/**
 * \brief           Test custom allocator usage
 */
TEST_F(XglAllocatorTest, CustomAllocatorUsage) {
    TestAllocator* test_alloc = TestAllocator::get_instance();
    test_alloc->reset();

    xgl_allocator_t custom_allocator = {
        .malloc = TestAllocator::test_malloc,
        .free = TestAllocator::test_free,
        .user_data = nullptr
    };

    /* Allocate using custom allocator */
    void* ptr1 = xgl_alloc(&custom_allocator, 100);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_EQ(test_alloc->alloc_count, 1);
    EXPECT_EQ(test_alloc->total_allocated, 100);

    void* ptr2 = xgl_alloc(&custom_allocator, 200);
    ASSERT_NE(ptr2, nullptr);
    EXPECT_EQ(test_alloc->alloc_count, 2);
    EXPECT_EQ(test_alloc->total_allocated, 300);

    /* Free using custom allocator */
    xgl_free(&custom_allocator, ptr1);
    EXPECT_EQ(test_alloc->free_count, 1);

    xgl_free(&custom_allocator, ptr2);
    EXPECT_EQ(test_alloc->free_count, 2);
}

/*---------------------------------------------------------------------------*/
/* Tracking Allocator Tests                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test tracking allocator initialization
 */
TEST_F(XglAllocatorTest, TrackingAllocatorInit) {
    xgl_tracking_allocator_t tracker;

    int result = xgl_tracking_allocator_init(&tracker, nullptr);
    EXPECT_EQ(result, 0);
    EXPECT_NE(tracker.underlying, nullptr);
    EXPECT_NE(tracker.base.malloc, nullptr);
    EXPECT_NE(tracker.base.free, nullptr);
}

/**
 * \brief           Test tracking allocator with NULL parameter
 */
TEST_F(XglAllocatorTest, TrackingAllocatorInitNull) {
    int result = xgl_tracking_allocator_init(nullptr, nullptr);
    EXPECT_EQ(result, -1);
}

/**
 * \brief           Test tracking allocator statistics
 */
TEST_F(XglAllocatorTest, TrackingAllocatorStats) {
    xgl_tracking_allocator_t tracker;
    xgl_allocator_stats_t stats;

    /* Initialize tracker */
    ASSERT_EQ(xgl_tracking_allocator_init(&tracker, nullptr), 0);

    /* Get initial statistics */
    xgl_tracking_allocator_get_stats(&tracker, &stats);
    EXPECT_EQ(stats.total_allocated, 0);
    EXPECT_EQ(stats.total_freed, 0);
    EXPECT_EQ(stats.current_allocated, 0);
    EXPECT_EQ(stats.peak_allocated, 0);
    EXPECT_EQ(stats.alloc_count, 0);
    EXPECT_EQ(stats.free_count, 0);
}

/**
 * \brief           Test tracking allocator reset statistics
 */
TEST_F(XglAllocatorTest, TrackingAllocatorResetStats) {
    xgl_tracking_allocator_t tracker;

    /* Initialize tracker */
    ASSERT_EQ(xgl_tracking_allocator_init(&tracker, nullptr), 0);

    /* Manually set some statistics */
    tracker.stats.total_allocated = 1000;
    tracker.stats.current_allocated = 500;
    tracker.stats.peak_allocated = 800;
    tracker.stats.alloc_count = 10;
    tracker.stats.free_count = 5;

    /* Reset statistics */
    xgl_tracking_allocator_reset_stats(&tracker);

    /* Check reset values */
    EXPECT_EQ(tracker.stats.total_allocated, 500);  /* Reset to current */
    EXPECT_EQ(tracker.stats.total_freed, 0);
    EXPECT_EQ(tracker.stats.current_allocated, 500);  /* Preserved */
    EXPECT_EQ(tracker.stats.peak_allocated, 500);  /* Reset to current */
    EXPECT_EQ(tracker.stats.alloc_count, 0);
    EXPECT_EQ(tracker.stats.free_count, 0);
}

/**
 * \brief           Test getting interface from tracking allocator
 */
TEST_F(XglAllocatorTest, TrackingAllocatorGetInterface) {
    xgl_tracking_allocator_t tracker;

    /* Initialize tracker */
    ASSERT_EQ(xgl_tracking_allocator_init(&tracker, nullptr), 0);

    /* Get interface */
    xgl_allocator_t* interface = xgl_tracking_allocator_get_interface(&tracker);
    ASSERT_NE(interface, nullptr);
    EXPECT_EQ(interface, &tracker.base);
    EXPECT_NE(interface->malloc, nullptr);
    EXPECT_NE(interface->free, nullptr);
}

/**
 * \brief           Test tracking allocator interface allocation path
 */
TEST_F(XglAllocatorTest, TrackingAllocatorInterfaceAllocatesAndTracks) {
    xgl_tracking_allocator_t tracker;
    xgl_allocator_stats_t stats;

    ASSERT_EQ(xgl_tracking_allocator_init(&tracker, nullptr), 0);

    xgl_allocator_t* interface = xgl_tracking_allocator_get_interface(&tracker);
    ASSERT_NE(interface, nullptr);

    void* ptr = xgl_alloc(interface, 64);
    ASSERT_NE(ptr, nullptr);
    memset(ptr, 0xAB, 64);

    xgl_tracking_allocator_get_stats(&tracker, &stats);
    EXPECT_EQ(stats.alloc_count, 1U);
    EXPECT_EQ(stats.total_allocated, 64U);
    EXPECT_EQ(stats.current_allocated, 64U);

    xgl_free(interface, ptr);

    xgl_tracking_allocator_get_stats(&tracker, &stats);
    EXPECT_EQ(stats.free_count, 1U);
    EXPECT_EQ(stats.total_freed, 64U);
    EXPECT_EQ(stats.current_allocated, 0U);
}

TEST_F(XglAllocatorTest, TrackingAllocatorDirectCallbacksFailClosed) {
    xgl_tracking_allocator_t first;
    xgl_tracking_allocator_t second;
    xgl_allocator_stats_t first_stats;
    xgl_allocator_stats_t second_stats;

    ASSERT_EQ(xgl_tracking_allocator_init(&first, nullptr), 0);
    ASSERT_EQ(xgl_tracking_allocator_init(&second, nullptr), 0);

    void* ptr = first.base.malloc(16);
    EXPECT_EQ(ptr, nullptr);

    xgl_tracking_allocator_get_stats(&first, &first_stats);
    xgl_tracking_allocator_get_stats(&second, &second_stats);
    EXPECT_EQ(first_stats.alloc_count, 0U);
    EXPECT_EQ(second_stats.alloc_count, 0U);
}

/**
 * \brief           Test tracking allocator phase statistics
 */
TEST_F(XglAllocatorTest, TrackingAllocatorPhaseStats) {
    xgl_tracking_allocator_t tracker;
    xgl_allocator_phase_stats_t phase_stats;

    ASSERT_EQ(xgl_tracking_allocator_init(&tracker, nullptr), 0);
    xgl_allocator_t* interface = xgl_tracking_allocator_get_interface(&tracker);
    ASSERT_NE(interface, nullptr);

    void* init_ptr = xgl_alloc(interface, 32);
    ASSERT_NE(init_ptr, nullptr);

    xgl_tracking_allocator_set_phase(&tracker, XGL_ALLOCATOR_PHASE_RUNTIME_TX);
    void* tx_ptr = xgl_alloc(interface, 48);
    ASSERT_NE(tx_ptr, nullptr);

    xgl_tracking_allocator_get_phase_stats(&tracker, &phase_stats);
    EXPECT_EQ(phase_stats.phase[XGL_ALLOCATOR_PHASE_INIT].alloc_count, 1U);
    EXPECT_EQ(phase_stats.phase[XGL_ALLOCATOR_PHASE_INIT].total_allocated, 32U);
    EXPECT_EQ(phase_stats.phase[XGL_ALLOCATOR_PHASE_RUNTIME_TX].alloc_count, 1U);
    EXPECT_EQ(phase_stats.phase[XGL_ALLOCATOR_PHASE_RUNTIME_TX].total_allocated, 48U);

    xgl_free(interface, init_ptr);
    xgl_free(interface, tx_ptr);

    xgl_tracking_allocator_get_phase_stats(&tracker, &phase_stats);
    EXPECT_EQ(phase_stats.phase[XGL_ALLOCATOR_PHASE_INIT].current_allocated, 0U);
    EXPECT_EQ(phase_stats.phase[XGL_ALLOCATOR_PHASE_RUNTIME_TX].current_allocated, 0U);
}

/**
 * \brief           Test tracking allocator with NULL parameter
 */
TEST_F(XglAllocatorTest, TrackingAllocatorGetInterfaceNull) {
    xgl_allocator_t* interface = xgl_tracking_allocator_get_interface(nullptr);
    EXPECT_EQ(interface, nullptr);
}

/**
 * \brief           Test tracking allocator get stats with NULL
 */
TEST_F(XglAllocatorTest, TrackingAllocatorGetStatsNull) {
    xgl_tracking_allocator_t tracker;
    xgl_allocator_stats_t stats;

    /* Should not crash */
    xgl_tracking_allocator_get_stats(nullptr, &stats);
    xgl_tracking_allocator_get_stats(&tracker, nullptr);
}

/**
 * \brief           Test tracking allocator reset with NULL
 */
TEST_F(XglAllocatorTest, TrackingAllocatorResetStatsNull) {
    /* Should not crash */
    xgl_tracking_allocator_reset_stats(nullptr);
}

/*---------------------------------------------------------------------------*/
/* Integration Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test allocator with multiple allocations
 */
TEST_F(XglAllocatorTest, MultipleAllocations) {
    const size_t num_allocs = 10;
    void* ptrs[num_allocs];

    /* Allocate multiple blocks */
    for (size_t i = 0; i < num_allocs; i++) {
        ptrs[i] = xgl_alloc(nullptr, (i + 1) * 64);
        ASSERT_NE(ptrs[i], nullptr);
        memset(ptrs[i], (int)i, (i + 1) * 64);
    }

    /* Free all blocks */
    for (size_t i = 0; i < num_allocs; i++) {
        xgl_free(nullptr, ptrs[i]);
    }
}

/**
 * \brief           Test allocator with large allocation
 */
TEST_F(XglAllocatorTest, LargeAllocation) {
    const size_t large_size = 1024 * 1024;  /* 1 MB */

    void* ptr = xgl_alloc(nullptr, large_size);
    ASSERT_NE(ptr, nullptr);

    /* Write pattern */
    memset(ptr, 0xDD, large_size);

    /* Free */
    xgl_free(nullptr, ptr);
}
