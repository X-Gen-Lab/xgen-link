/**
 * \file            test_memory_properties.cpp
 * \brief           Memory management property tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "property_framework.h"
#include "../mocks/mock_allocator.h"
#include "../mocks/mock_phy.h"
#include <xgl/xgl.h>
#include <xgl/internal/xgl_allocator.h>

using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;
using ::testing::AtLeast;

static uint8_t random_valid_source_id(PropertyTestGenerator& gen) {
    return static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
}

/* Feature: x-gen-link, Property 7: Custom Allocator Usage */
TEST(XglMemoryProperties, CustomAllocatorUsage) {
    PropertyTestGenerator gen;

    /* Run property test with multiple iterations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        MockAllocator mock_alloc;

        /* Setup expectations: all allocations should go through custom allocator */
        EXPECT_CALL(mock_alloc, malloc_impl(_))
            .Times(AtLeast(1))
            .WillRepeatedly(Invoke([](size_t size) -> void* {
                return std::malloc(size);
            }));

        EXPECT_CALL(mock_alloc, free_impl(_))
            .Times(AtLeast(1))
            .WillRepeatedly(Invoke([](void* ptr) {
                std::free(ptr);
            }));

        /* Create configuration with custom allocator */
        xgl_config_t config;
        xgl_config_get_default(&config);
        config.source_id = random_valid_source_id(gen);
        config.memory.allocator = mock_alloc.get_allocator();

        /* Track allocations before instance creation */
        size_t alloc_count_before = mock_alloc.get_alloc_count();

        /* Create instance - should use custom allocator */
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr) << "Failed to create instance with custom allocator";

        /* Verify that allocations occurred through custom allocator */
        EXPECT_GT(mock_alloc.get_alloc_count(), alloc_count_before)
            << "No allocations went through custom allocator during xgl_create()";
        EXPECT_GT(mock_alloc.get_total_allocated(), 0)
            << "Custom allocator was not used for memory allocation";

        /* Initialize instance - should also use custom allocator */
        size_t alloc_count_before_init = mock_alloc.get_alloc_count();
        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK) << "Failed to initialize instance: "
                               << xgl_error_string(err);

        /* Verify more allocations occurred during init */
        EXPECT_GT(mock_alloc.get_alloc_count(), alloc_count_before_init)
            << "No allocations went through custom allocator during xgl_init()";

        /* Track memory state before destroy */
        size_t allocated_before_destroy = mock_alloc.get_current_allocated();
        EXPECT_GT(allocated_before_destroy, 0)
            << "No memory currently allocated through custom allocator";

        /* Destroy instance - should free all memory through custom allocator */
        xgl_destroy(handle);

        /* Verify all memory was freed through custom allocator */
        EXPECT_EQ(mock_alloc.get_current_allocated(), 0)
            << "Memory leak detected: not all memory freed through custom allocator";
        EXPECT_EQ(mock_alloc.get_total_allocated(), mock_alloc.get_total_freed())
            << "Allocation/deallocation mismatch in custom allocator";

        /* Verify expectations were met */
        testing::Mock::VerifyAndClearExpectations(&mock_alloc);
    }
}

/* Feature: x-gen-link, Property 1: Memory Leak Prevention */
TEST(XglMemoryProperties, MemoryLeakPrevention) {
    PropertyTestGenerator gen;

    /* Run property test with multiple iterations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        MockAllocator mock_alloc;

        /* Setup expectations: track all allocations and frees */
        EXPECT_CALL(mock_alloc, malloc_impl(_))
            .WillRepeatedly(Invoke([](size_t size) -> void* {
                return std::malloc(size);
            }));

        EXPECT_CALL(mock_alloc, free_impl(_))
            .WillRepeatedly(Invoke([](void* ptr) {
                std::free(ptr);
            }));

        /* Generate random configuration */
        xgl_config_t config;
        xgl_config_get_default(&config);
        config.source_id = random_valid_source_id(gen);
        config.memory.tx_pool_size = 512 + (gen.random_uint16() % 4096);
        config.protocol.max_retry_count = 1 + (gen.random_uint8() % 10);
        config.protocol.window_size = 1 + (gen.random_uint8() % 16);
        config.protocol.max_frame_size = 64 + (gen.random_uint16() % 960);
        config.memory.rx_buffer_size = config.protocol.max_frame_size;
        config.memory.allocator = mock_alloc.get_allocator();

        /* Create instance */
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr) << "Failed to create instance in iteration " << iteration;

        /* Verify allocations occurred */
        size_t allocated_after_create = mock_alloc.get_current_allocated();
        EXPECT_GT(allocated_after_create, 0)
            << "No memory allocated after xgl_create() in iteration " << iteration;

        /* Initialize instance */
        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK) << "Failed to initialize instance in iteration " << iteration
                               << ": " << xgl_error_string(err);

        /* Verify more allocations occurred during init */
        size_t allocated_after_init = mock_alloc.get_current_allocated();
        EXPECT_GT(allocated_after_init, allocated_after_create)
            << "No additional memory allocated during xgl_init() in iteration " << iteration;

        /* Destroy instance */
        xgl_destroy(handle);

        /* CRITICAL: Verify all memory was freed (no leaks) */
        EXPECT_EQ(mock_alloc.get_current_allocated(), 0)
            << "Memory leak detected in iteration " << iteration
            << ": " << mock_alloc.get_current_allocated() << " bytes not freed"
            << " (allocated: " << mock_alloc.get_total_allocated()
            << ", freed: " << mock_alloc.get_total_freed() << ")";

        /* Verify allocation/deallocation balance */
        EXPECT_EQ(mock_alloc.get_total_allocated(), mock_alloc.get_total_freed())
            << "Allocation/deallocation mismatch in iteration " << iteration;

        testing::Mock::VerifyAndClearExpectations(&mock_alloc);
    }
}

/* Feature: x-gen-link, Property 8: Allocation Failure Handling */
TEST(XglMemoryProperties, AllocationFailureHandling) {
    PropertyTestGenerator gen;

    /* Test various allocation failure scenarios */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        MockAllocator mock_alloc;

        /* Randomly decide which allocation to fail */
        int fail_after_count = gen.random_uint8() % 10;  /* Fail after N allocations */
        int alloc_count = 0;

        /* Setup expectations: fail allocation after N successful allocations */
        EXPECT_CALL(mock_alloc, malloc_impl(_))
            .WillRepeatedly(Invoke([&alloc_count, fail_after_count](size_t size) -> void* {
                if (alloc_count >= fail_after_count) {
                    return nullptr;  /* Simulate allocation failure */
                }
                alloc_count++;
                return std::malloc(size);
            }));

        EXPECT_CALL(mock_alloc, free_impl(_))
            .WillRepeatedly(Invoke([](void* ptr) {
                if (ptr != nullptr) {
                    std::free(ptr);
                }
            }));

        /* Create configuration */
        xgl_config_t config;
        xgl_config_get_default(&config);
        config.source_id = random_valid_source_id(gen);
        config.memory.allocator = mock_alloc.get_allocator();

        /* Track memory before operation */
        size_t allocated_before = mock_alloc.get_current_allocated();

        /* Try to create instance - may fail due to allocation failure */
        xgl_handle_t handle = xgl_create(&config);

        if (handle == nullptr) {
            /* Creation failed - verify all partial allocations were cleaned up */
            EXPECT_EQ(mock_alloc.get_current_allocated(), allocated_before)
                << "Memory leak after failed xgl_create() in iteration " << iteration
                << ": partial allocations not cleaned up";
        } else {
            /* Creation succeeded - try initialization */
            xgl_error_t err = xgl_init(handle);

            if (err != XGL_OK) {
                /* Initialization failed - verify error code is appropriate */
                EXPECT_TRUE(err == XGL_ERR_NO_MEMORY || err == XGL_ERR_POOL_EXHAUSTED)
                    << "Unexpected error code after allocation failure in iteration " << iteration
                    << ": " << xgl_error_string(err);

                /* Don't call xgl_destroy after failed init - instance is not fully initialized */
                /* Just free the instance structure itself */
                xgl_free(config.memory.allocator, handle);

                /* Verify all memory was freed after failed init */
                EXPECT_EQ(mock_alloc.get_current_allocated(), 0)
                    << "Memory leak after failed xgl_init() in iteration " << iteration
                    << ": " << mock_alloc.get_current_allocated() << " bytes not freed";
            } else {
                /* Both create and init succeeded - clean up normally */
                xgl_destroy(handle);

                EXPECT_EQ(mock_alloc.get_current_allocated(), 0)
                    << "Memory leak after successful create/init/destroy in iteration " << iteration;
            }
        }

        /* Final verification: no memory leaks regardless of success/failure */
        EXPECT_EQ(mock_alloc.get_current_allocated(), 0)
            << "Memory leak detected in iteration " << iteration
            << " after handling allocation failure";

        testing::Mock::VerifyAndClearExpectations(&mock_alloc);
    }
}

/* Feature: x-gen-link, Property 9: Memory Pool Exhaustion */
TEST(XglMemoryProperties, MemoryPoolExhaustion) {
    PropertyTestGenerator gen;

    /* Run property test with multiple iterations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Setup simple PHY callbacks for routing */
        xgl_phy_ops_t phy = {
            .tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
                (void)data; (void)len; (void)user_data;  /* Unused */
                return XGL_OK;  /* Simulate successful transmission */
            },
            .rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
                (void)buffer; (void)len; (void)user_data;  /* Unused */
                return XGL_ERR_TIMEOUT;  /* No data to receive */
            },
            .user_data = nullptr
        };

        /* Setup route table */
        xgl_route_item_t route = {
            .target_id = 1,
            .phy = &phy,
            .max_frame_size = 96,
            .read_freq_hz = 100,
            .metric = 0
        };

        /* Create configuration with very small memory pool */
        xgl_config_t config;
        xgl_config_get_preset_tiny(&config);  /* Use tiny preset for minimal pool */
        config.source_id = random_valid_source_id(gen);
        config.protocol.max_frame_size = 96;
        config.memory.rx_buffer_size = config.protocol.max_frame_size;
        config.route_table = &route;
        config.route_table_len = 1;

        /* Create and initialize instance */
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr) << "Failed to create instance in iteration " << iteration;

        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK) << "Failed to initialize instance in iteration " << iteration
                               << ": " << xgl_error_string(err);

        /* Get initial statistics */
        xgl_statistics_t stats_before;
        err = xgl_stats_get(handle, &stats_before);
        ASSERT_EQ(err, XGL_OK) << "Failed to get statistics in iteration " << iteration;

        /* Try to send many packets under memory pressure */
        int send_attempts = 0;
        int successful_sends = 0;
        int resource_errors = 0;

        /* Generate larger data to consume more memory */
        std::vector<uint8_t> test_data = gen.random_bytes(48);  /* Larger packets */

        /* Attempt to send many packets to the configured route */
        for (int i = 0; i < 200; ++i) {  /* Many packets to stress memory */
            xgl_tx_data_t tx_data = {
                .target_id = 1,  /* Use configured route */
                .data_type = gen.random_uint8(),
                .data = test_data.data(),
                .data_len = test_data.size(),
                .reliable = true,  /* Reliable transmission keeps packets in queue */
                .priority = static_cast<uint8_t>(gen.random_uint8() % 8),
                .timeout_ms = 0
            };

            send_attempts++;
            xgl_error_t send_err = xgl_send(handle, &tx_data);

            if (send_err == XGL_OK) {
                successful_sends++;
            } else if (send_err == XGL_ERR_POOL_EXHAUSTED ||
                      send_err == XGL_ERR_NO_MEMORY ||
                      send_err == XGL_ERR_QUEUE_FULL ||
                      send_err == XGL_ERR_WINDOW_FULL) {
                /* Expected resource exhaustion errors */
                resource_errors++;
            } else {
                /* Unexpected error */
                FAIL() << "Unexpected error during send in iteration " << iteration
                       << ": " << xgl_error_string(send_err);
            }
        }

        /* Property: When memory pool is exhausted, system returns error without corrupting state */
        /* We verify this by checking that the instance remains functional */

        /* Get statistics after sending */
        xgl_statistics_t stats_after;
        err = xgl_stats_get(handle, &stats_after);
        ASSERT_EQ(err, XGL_OK)
            << "Instance corrupted after memory pressure in iteration " << iteration
            << ": cannot get statistics";

        /* Verify that instance state is not corrupted */
        /* Statistics should be consistent (no negative values, monotonic increases) */
        EXPECT_GE(stats_after.datalink.tx_packets, stats_before.datalink.tx_packets)
            << "TX packet count decreased in iteration " << iteration;
        EXPECT_GE(stats_after.datalink.tx_bytes, stats_before.datalink.tx_bytes)
            << "TX byte count decreased in iteration " << iteration;

        /* Try to get statistics again to verify instance is still functional */
        xgl_statistics_t stats_verify;
        err = xgl_stats_get(handle, &stats_verify);
        EXPECT_EQ(err, XGL_OK)
            << "Instance corrupted after memory pressure in iteration " << iteration
            << ": cannot get statistics on second attempt";

        /* Verify statistics are consistent across multiple reads */
        EXPECT_EQ(stats_verify.datalink.tx_packets, stats_after.datalink.tx_packets)
            << "Statistics inconsistent after memory pressure in iteration " << iteration;
        EXPECT_EQ(stats_verify.datalink.tx_bytes, stats_after.datalink.tx_bytes)
            << "Statistics inconsistent after memory pressure in iteration " << iteration;

        /* Verify we attempted to send packets and some succeeded */
        EXPECT_EQ(send_attempts, 200)
            << "Did not attempt all sends in iteration " << iteration;
        EXPECT_GT(successful_sends, 0)
            << "No successful sends in iteration " << iteration;

        /* The key property: system handled memory pressure without corruption */
        /* Whether we got resource errors or not, the system should remain functional */

        /* Clean up */
        xgl_destroy(handle);
    }
}
