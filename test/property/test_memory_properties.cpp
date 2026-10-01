/**
 * \file            test_memory_properties.cpp
 * \brief           Memory management property tests
 * \author          X-Gen Lab
 */

#include "test_host_allocator.h"

#include <xgl/xgl.h>

#include <cstddef>
#include <cstring>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vector>
#include <xgen/memory/allocator.h>

#include "../mocks/mock_allocator.h"
#include "../mocks/mock_phy.h"
#include "property_framework.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::Return;

static uint8_t random_valid_source_id(PropertyTestGenerator& gen) {
    return static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
}

/**
 * \brief           Synchronous peer used to exercise caller-owned workspaces
 */
struct WorkspaceEndpoint {
    WorkspaceEndpoint* remote = nullptr;
    std::vector<uint8_t> incoming;
    std::vector<uint8_t> delivered;
    size_t deliveries = 0U;
    bool busy = false;
};

/**
 * \brief           Transfer a bounded frame without reentering the protocol
 */
static xgl_error_t workspace_tx(const uint8_t* data, size_t size, void* ctx) {
    auto* endpoint = static_cast<WorkspaceEndpoint*>(ctx);
    auto& incoming = endpoint->remote->incoming;
    if (size > 4096U - incoming.size()) {
        return XGL_ERR_BUSY;
    }
    incoming.insert(incoming.end(), data, data + size);
    return XGL_OK;
}

/**
 * \brief           Consume the bytes that fit the protocol-owned RX cache
 */
static xgl_error_t workspace_rx(uint8_t* data, size_t* size, void* ctx) {
    auto* endpoint = static_cast<WorkspaceEndpoint*>(ctx);
    if (*size > endpoint->incoming.size()) {
        *size = endpoint->incoming.size();
    }
    if (*size != 0U) {
        std::memcpy(data, endpoint->incoming.data(), *size);
        endpoint->incoming.erase(endpoint->incoming.begin(),
                                 endpoint->incoming.begin() +
                                     static_cast<std::ptrdiff_t>(*size));
    }
    return XGL_OK;
}

/**
 * \brief           Retain a delivered message or apply application backpressure
 */
static xgl_error_t workspace_accept(xgl_handle_t, uint16_t, uint8_t,
                                    const uint8_t* data, size_t size,
                                    void* ctx) {
    auto* endpoint = static_cast<WorkspaceEndpoint*>(ctx);
    if (endpoint->busy) {
        return XGL_ERR_BUSY;
    }
    endpoint->delivered.assign(data, data + size);
    ++endpoint->deliveries;
    return XGL_OK;
}

/**
 * \brief           Verify window capacity, ACK recycling and fragment retention
 */
static void verify_static_workspace(bool fragmentation) {
    WorkspaceEndpoint a, b;
    a.remote = &b;
    b.remote = &a;
    xgl_phy_ops_t phy_a = {workspace_tx, workspace_rx, &a};
    xgl_phy_ops_t phy_b = {workspace_tx, workspace_rx, &b};
    xgl_route_item_t route_a = {2U, &phy_a, 96U, 1000U, 0U};
    xgl_route_item_t route_b = {1U, &phy_b, 96U, 1000U, 0U};
    MockAllocator backend;
    EXPECT_CALL(backend, malloc_impl(_)).Times(0);
    EXPECT_CALL(backend, free_impl(_)).Times(0);

    xgl_config_t config_a;
    xgl_config_get_preset_tiny(&config_a);
    config_a.protocol.window_size = 2U;
    config_a.protocol.max_frame_size = 96U;
    config_a.memory.rx_buffer_size = 96U;
    config_a.memory.allocator = backend.get_allocator();
    config_a.features.max_peers = 1U;
    config_a.features.max_tx_packets = 2U;
    config_a.features.max_rx_buffered_packets = 1U;
    config_a.features.enable_fragmentation = fragmentation;
    config_a.features.max_message_size = 320U;
    config_a.features.max_reassembly_slots = 1U;
    config_a.features.max_reassembly_bytes = 320U;
    config_a.features.max_tx_message_bytes = 320U;
    config_a.route_table = &route_a;
    config_a.route_table_len = 1U;
    config_a.rx_accept_callback = workspace_accept;
    config_a.callback_user_data = &a;
    xgl_config_t config_b = config_a;
    config_b.source_id = 2U;
    config_b.route_table = &route_b;
    config_b.callback_user_data = &b;

    xgl_memory_requirements_t required{};
    ASSERT_EQ(xgl_memory_requirements(&config_a, &required), XGL_OK);
    const size_t words = (required.size + sizeof(xgm_max_align_t) - 1U) /
                         sizeof(xgm_max_align_t);
    std::vector<xgm_max_align_t> storage_a(words), storage_b(words);
    std::vector<uint8_t> payload(fragmentation ? 300U : 1U, 0x65U);
    xgl_tx_data_t tx{};
    tx.target_id = 2U;
    tx.data = payload.data();
    tx.data_len = payload.size();
    tx.reliable = true;
    tx.timeout_ms = 20U;
    const xgl_work_budget_t budget = {512U, 32U};

    for (size_t cycle = 0U; cycle < 2U; ++cycle) {
        xgl_handle_t sender = nullptr;
        xgl_handle_t receiver = nullptr;
        ASSERT_EQ(xgl_init_static(&config_a, storage_a.data(), required.size,
                                  &sender),
                  XGL_OK);
        ASSERT_EQ(xgl_init_static(&config_b, storage_b.data(), required.size,
                                  &receiver),
                  XGL_OK);
        ASSERT_EQ(xgl_send_at(sender, &tx, 0U), XGL_OK);
        if (!fragmentation) {
            ASSERT_EQ(xgl_send_at(sender, &tx, 0U), XGL_OK);
            EXPECT_EQ(xgl_send_at(sender, &tx, 0U), XGL_ERR_WINDOW_FULL);
        } else {
            EXPECT_EQ(xgl_send_at(sender, &tx, 0U), XGL_ERR_BUSY);
        }
        b.busy = fragmentation;
        for (uint32_t tick = 0U; tick < 200U; ++tick) {
            if (tick == 8U) {
                b.busy = false;
            }
            (void)xgl_step(receiver, tick, &budget);
            (void)xgl_step(sender, tick, &budget);
        }
        EXPECT_EQ(b.deliveries, (cycle + 1U) * (fragmentation ? 1U : 2U));
        EXPECT_EQ(b.delivered, payload);
        xgl_destroy(sender);
        xgl_destroy(receiver);
        a.incoming.clear();
        b.incoming.clear();
    }
}

TEST(XglMemoryProperties, StaticWindowResourcesAreRecycledWithoutBackend) {
    verify_static_workspace(false);
}

#if XGL_FEATURE_FRAGMENTATION
TEST(XglMemoryProperties, StaticFragmentsCrossWindowsAndRetainBusyDelivery) {
    verify_static_workspace(true);
}
#endif

/* Feature: x-gen-link, Property 7: Custom Allocator Usage */
TEST(XglMemoryProperties, CustomAllocatorUsage) {
    PropertyTestGenerator gen;

    /* Run property test with multiple iterations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        MockAllocator mock_alloc;

        /* Setup expectations: all allocations should go through custom
         * allocator */
        EXPECT_CALL(mock_alloc, malloc_impl(_))
            .Times(AtLeast(1))
            .WillRepeatedly(
                Invoke([](size_t size) -> void* { return std::malloc(size); }));

        EXPECT_CALL(mock_alloc, free_impl(_))
            .Times(AtLeast(1))
            .WillRepeatedly(Invoke([](void* ptr) { std::free(ptr); }));

        /* Create configuration with custom allocator */
        xgl_config_t config;
        xgl_config_get_default(&config);
        config.source_id = random_valid_source_id(gen);
        config.memory.allocator = mock_alloc.get_allocator();

        /* Track allocations before instance creation */
        size_t alloc_count_before = mock_alloc.get_alloc_count();

        /* Create instance - should use custom allocator */
        xgl_test_use_host_allocator(&config);
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr)
            << "Failed to create instance with custom allocator";

        /* Verify that allocations occurred through custom allocator */
        EXPECT_GT(mock_alloc.get_alloc_count(), alloc_count_before)
            << "No allocations went through custom allocator during "
               "xgl_create()";
        EXPECT_GT(mock_alloc.get_total_allocated(), 0)
            << "Custom allocator was not used for memory allocation";

        /* Initialization consumes the workspace already reserved at creation.
         */
        size_t alloc_count_before_init = mock_alloc.get_alloc_count();
        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK)
            << "Failed to initialize instance: " << xgl_error_string(err);

        EXPECT_EQ(mock_alloc.get_alloc_count(), alloc_count_before_init)
            << "xgl_init() requested storage beyond the reserved workspace";

        /* Track memory state before destroy */
        size_t allocated_before_destroy = mock_alloc.get_current_allocated();
        EXPECT_GT(allocated_before_destroy, 0)
            << "No memory currently allocated through custom allocator";

        /* Destroy instance - should free all memory through custom allocator */
        xgl_destroy(handle);

        /* Verify all memory was freed through custom allocator */
        EXPECT_EQ(mock_alloc.get_current_allocated(), 0)
            << "Memory leak detected: not all memory freed through custom "
               "allocator";
        EXPECT_EQ(mock_alloc.get_total_allocated(),
                  mock_alloc.get_total_freed())
            << "Allocation/deallocation mismatch in custom allocator";

        /* Verify expectations were met */
        testing::Mock::VerifyAndClearExpectations(&mock_alloc);
    }
}

/* Feature: x-gen-link, Property 1: Memory Leak Prevention */
TEST(XglMemoryProperties, MemoryLeakPrevention) {
    PropertyTestGenerator gen;

    /* Run property test with multiple iterations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        MockAllocator mock_alloc;

        /* Setup expectations: track all allocations and frees */
        EXPECT_CALL(mock_alloc, malloc_impl(_))
            .WillRepeatedly(
                Invoke([](size_t size) -> void* { return std::malloc(size); }));

        EXPECT_CALL(mock_alloc, free_impl(_))
            .WillRepeatedly(Invoke([](void* ptr) { std::free(ptr); }));

        /* Generate random configuration */
        xgl_config_t config;
        xgl_config_get_default(&config);
        config.source_id = random_valid_source_id(gen);
        config.protocol.max_retry_count = 1 + (gen.random_uint8() % 10);
        config.protocol.window_size = 1 + (gen.random_uint8() % 16);
        config.protocol.max_frame_size = 64 + (gen.random_uint16() % 960);
        config.memory.rx_buffer_size = config.protocol.max_frame_size;
        config.memory.allocator = mock_alloc.get_allocator();

        /* Create instance */
        xgl_test_use_host_allocator(&config);
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr)
            << "Failed to create instance in iteration " << iteration;

        /* Verify allocations occurred */
        size_t allocated_after_create = mock_alloc.get_current_allocated();
        EXPECT_GT(allocated_after_create, 0)
            << "No memory allocated after xgl_create() in iteration "
            << iteration;

        /* Initialize instance */
        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK) << "Failed to initialize instance in iteration "
                               << iteration << ": " << xgl_error_string(err);

        /* Runtime resources were all reserved by the one creation allocation.
         */
        size_t allocated_after_init = mock_alloc.get_current_allocated();
        EXPECT_EQ(allocated_after_init, allocated_after_create)
            << "Initialization allocated outside the workspace in iteration "
            << iteration;

        /* Destroy instance */
        xgl_destroy(handle);

        /* CRITICAL: Verify all memory was freed (no leaks) */
        EXPECT_EQ(mock_alloc.get_current_allocated(), 0)
            << "Memory leak detected in iteration " << iteration << ": "
            << mock_alloc.get_current_allocated() << " bytes not freed"
            << " (allocated: " << mock_alloc.get_total_allocated()
            << ", freed: " << mock_alloc.get_total_freed() << ")";

        /* Verify allocation/deallocation balance */
        EXPECT_EQ(mock_alloc.get_total_allocated(),
                  mock_alloc.get_total_freed())
            << "Allocation/deallocation mismatch in iteration " << iteration;

        testing::Mock::VerifyAndClearExpectations(&mock_alloc);
    }
}

/* Feature: x-gen-link, Property 8: Allocation Failure Handling */
TEST(XglMemoryProperties, AllocationFailureHandling) {
    PropertyTestGenerator gen;

    /* Test various allocation failure scenarios */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        MockAllocator mock_alloc;

        /* Creation is the only backend allocation; cover failure and success.
         */
        int fail_after_count = iteration % 2;
        int alloc_count = 0;

        /* Setup expectations: fail allocation after N successful allocations */
        EXPECT_CALL(mock_alloc, malloc_impl(_))
            .WillRepeatedly(
                Invoke([&alloc_count, fail_after_count](size_t size) -> void* {
                    if (alloc_count >= fail_after_count) {
                        return nullptr; /* Simulate allocation failure */
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
        xgl_test_use_host_allocator(&config);
        xgl_handle_t handle = xgl_create(&config);

        if (handle == nullptr) {
            /* Creation failed - verify all partial allocations were cleaned up
             */
            EXPECT_EQ(mock_alloc.get_current_allocated(), allocated_before)
                << "Memory leak after failed xgl_create() in iteration "
                << iteration << ": partial allocations not cleaned up";
        } else {
            /* Creation succeeded - try initialization */
            xgl_error_t err = xgl_init(handle);

            EXPECT_EQ(err, XGL_OK)
                << "Pre-reserved workspace could not initialize";
            EXPECT_EQ(alloc_count, 1)
                << "Initialization called the backend after workspace creation";
            xgl_destroy(handle);
            EXPECT_EQ(mock_alloc.get_current_allocated(), 0)
                << "Workspace was not released after destruction";
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
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        /* Setup simple PHY callbacks for routing */
        xgl_phy_ops_t phy = {};
        phy.tx = [](const uint8_t* data, size_t len,
                    void* user_data) -> xgl_error_t {
            (void)data;
            (void)len;
            (void)user_data; /* Unused */
            return XGL_OK;   /* Simulate successful transmission */
        };
        phy.rx = [](uint8_t* buffer, size_t* len,
                    void* user_data) -> xgl_error_t {
            (void)buffer;
            (void)len;
            (void)user_data;        /* Unused */
            return XGL_ERR_TIMEOUT; /* No data to receive */
        };
        phy.user_data = nullptr;

        /* Setup route table */
        xgl_route_item_t route = {};
        route.target_id = 1;
        route.phy = &phy;
        route.max_frame_size = 96;
        route.read_freq_hz = 100;
        route.metric = 0;

        /* Create configuration with very small memory pool */
        xgl_config_t config;
        xgl_config_get_preset_tiny(
            &config); /* Use tiny preset for minimal pool */
        config.source_id = random_valid_source_id(gen);
        config.protocol.max_frame_size = 96;
        config.memory.rx_buffer_size = config.protocol.max_frame_size;
        config.route_table = &route;
        config.route_table_len = 1;

        /* Create and initialize instance */
        xgl_test_use_host_allocator(&config);
        xgl_handle_t handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr)
            << "Failed to create instance in iteration " << iteration;

        xgl_error_t err = xgl_init(handle);
        ASSERT_EQ(err, XGL_OK) << "Failed to initialize instance in iteration "
                               << iteration << ": " << xgl_error_string(err);

        /* Get initial statistics */
        xgl_statistics_t stats_before;
        err = xgl_stats_get(handle, &stats_before);
        ASSERT_EQ(err, XGL_OK)
            << "Failed to get statistics in iteration " << iteration;

        /* Try to send many packets under memory pressure */
        int send_attempts = 0;
        int successful_sends = 0;
        int resource_errors = 0;

        /* Generate larger data to consume more memory */
        std::vector<uint8_t> test_data =
            gen.random_bytes(48); /* Larger packets */

        /* Attempt to send many packets to the configured route */
        for (int i = 0; i < 200; ++i) { /* Many packets to stress memory */
            xgl_tx_data_t tx_data = {};
            tx_data.target_id = 1;
            /* Use configured route */
            tx_data.data_type = gen.random_uint8();
            tx_data.data = test_data.data();
            tx_data.data_len = test_data.size();
            tx_data.reliable = true;
            /* Reliable transmission keeps packets in queue */
            tx_data.priority = static_cast<uint8_t>(gen.random_uint8() % 8);
            tx_data.timeout_ms = 0;

            send_attempts++;
            xgl_error_t send_err = xgl_send_at(handle, &tx_data, 0U);

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
                FAIL() << "Unexpected error during send in iteration "
                       << iteration << ": " << xgl_error_string(send_err);
            }
        }

        /* Property: When memory pool is exhausted, system returns error without
         * corrupting state */
        /* We verify this by checking that the instance remains functional */

        /* Get statistics after sending */
        xgl_statistics_t stats_after;
        err = xgl_stats_get(handle, &stats_after);
        ASSERT_EQ(err, XGL_OK)
            << "Instance corrupted after memory pressure in iteration "
            << iteration << ": cannot get statistics";

        /* Verify that instance state is not corrupted */
        /* Statistics should be consistent (no negative values, monotonic
         * increases) */
        EXPECT_GE(stats_after.datalink.tx_packets,
                  stats_before.datalink.tx_packets)
            << "TX packet count decreased in iteration " << iteration;
        EXPECT_GE(stats_after.datalink.tx_bytes, stats_before.datalink.tx_bytes)
            << "TX byte count decreased in iteration " << iteration;

        /* Try to get statistics again to verify instance is still functional */
        xgl_statistics_t stats_verify;
        err = xgl_stats_get(handle, &stats_verify);
        EXPECT_EQ(err, XGL_OK)
            << "Instance corrupted after memory pressure in iteration "
            << iteration << ": cannot get statistics on second attempt";

        /* Verify statistics are consistent across multiple reads */
        EXPECT_EQ(stats_verify.datalink.tx_packets,
                  stats_after.datalink.tx_packets)
            << "Statistics inconsistent after memory pressure in iteration "
            << iteration;
        EXPECT_EQ(stats_verify.datalink.tx_bytes, stats_after.datalink.tx_bytes)
            << "Statistics inconsistent after memory pressure in iteration "
            << iteration;

        /* Verify we attempted to send packets and some succeeded */
        EXPECT_EQ(send_attempts, 200)
            << "Did not attempt all sends in iteration " << iteration;
        EXPECT_GT(successful_sends, 0)
            << "No successful sends in iteration " << iteration;

        /* The key property: system handled memory pressure without corruption
         */
        /* Whether we got resource errors or not, the system should remain
         * functional */

        /* Clean up */
        xgl_destroy(handle);
    }
}
