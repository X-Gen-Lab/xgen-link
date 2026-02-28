/**
 * \file            test_allocator.cpp
 * \brief           Custom allocator unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/xgl_allocator.h>
#include <cstring>

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
    ASSERT_NE(ptr, nullptr);
    
    /* Write to memory */
    memset(ptr, 0xBB, 256);
    
    /* Free memory */
    xgl_free(nullptr, ptr);
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

