/**
 * \file            test_footprint.cpp
 * \brief           Workspace ownership and runtime allocation accounting
 * \author          X-Gen Lab
 */

#include "test_host_allocator.h"

#include <xgl/xgl.h>

#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <internal/xgl_allocator.h>

namespace {

/**
 * \brief           Per-instance backend counters without shared global state
 */
struct CountingAllocator {
    size_t allocations = 0U;
    size_t frees = 0U;
    size_t bytes = 0U;
};

/**
 * \brief           Record the sole allocation used to reserve a workspace
 */
void* counting_alloc(void* ctx, size_t size) {
    auto* counter = static_cast<CountingAllocator*>(ctx);
    void* ptr = std::malloc(size);
    if (ptr != nullptr) {
        ++counter->allocations;
        counter->bytes += size;
    }
    return ptr;
}

/**
 * \brief           Record release through the same explicit backend context
 */
void counting_free(void* ctx, void* ptr) {
    if (ptr != nullptr) {
        ++static_cast<CountingAllocator*>(ctx)->frees;
    }
    std::free(ptr);
}

xgl_error_t null_tx(const uint8_t*, size_t, void*) {
    return XGL_OK;
}

xgl_error_t null_rx(uint8_t*, size_t* size, void*) {
    *size = 0U;
    return XGL_OK;
}

}  // namespace

TEST(XglFootprintTest, CreateReservesExactlyTheMeasuredWorkspaceOnce) {
    CountingAllocator counter;
    xgl_allocator_t allocator = {&counter, counting_alloc, counting_free};
    xgl_phy_ops_t phy = {null_tx, null_rx, nullptr};
    xgl_route_item_t route = {2U, &phy, 128U, 100U, 1U};
    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    config.memory.allocator = &allocator;
    config.route_table = &route;
    config.route_table_len = 1U;
    xgl_memory_requirements_t requirements{};
    ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(counter.allocations, 1U);
    EXPECT_EQ(counter.bytes, requirements.size);
    EXPECT_EQ(xgl_init(handle), XGL_OK);
    EXPECT_EQ(counter.allocations, 1U);
    EXPECT_EQ(counter.frees, 0U);
    xgl_destroy(handle);
    EXPECT_EQ(counter.frees, 1U);
}

TEST(XglFootprintTest, SendsReuseReservedStorageWithoutBackendAllocations) {
    CountingAllocator counter;
    xgl_allocator_t allocator = {&counter, counting_alloc, counting_free};
    xgl_phy_ops_t phy = {null_tx, null_rx, nullptr};
    xgl_route_item_t route = {2U, &phy, 128U, 100U, 1U};
    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    config.memory.allocator = &allocator;
    config.route_table = &route;
    config.route_table_len = 1U;
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(xgl_init(handle), XGL_OK);
    ASSERT_EQ(counter.allocations, 1U);

    const uint8_t payload[] = {1U, 2U, 3U, 4U};
    xgl_tx_data_t tx{};
    tx.target_id = 2U;
    tx.data_type = 1U;
    tx.data = payload;
    tx.data_len = sizeof(payload);
    for (uint32_t tick = 0U; tick < 1000U; ++tick) {
        ASSERT_EQ(xgl_send_at(handle, &tx, tick), XGL_OK);
    }
    tx.reliable = true;
    for (size_t slot = 0U; slot < config.protocol.window_size; ++slot) {
        ASSERT_EQ(xgl_send_at(handle, &tx, 1000U), XGL_OK);
    }
    EXPECT_EQ(xgl_send_at(handle, &tx, 1000U), XGL_ERR_WINDOW_FULL);
    EXPECT_EQ(counter.allocations, 1U);
    EXPECT_EQ(counter.frees, 0U);
    xgl_destroy(handle);
    EXPECT_EQ(counter.frees, 1U);
}

TEST(XglFootprintTest, SharedPhyReservesOnlyOneReceiveLink) {
    xgl_phy_ops_t phys[] = {{null_tx, null_rx, nullptr},
                            {null_tx, null_rx, nullptr},
                            {null_tx, null_rx, nullptr}};
    xgl_route_item_t routes[] = {{2U, &phys[0], 128U, 100U, 1U},
                                 {3U, &phys[0], 128U, 50U, 1U},
                                 {4U, &phys[0], 128U, 25U, 1U}};
    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    config.route_table = routes;
    config.route_table_len = 3U;
    xgl_memory_requirements_t shared{};
    ASSERT_EQ(xgl_memory_requirements(&config, &shared), XGL_OK);

    routes[1].phy = &phys[1];
    routes[2].phy = &phys[2];
    xgl_memory_requirements_t separate{};
    ASSERT_EQ(xgl_memory_requirements(&config, &separate), XGL_OK);

    /* Sharing PHYs saves their parser descriptors as well as RX bytes. */
    EXPECT_GT(separate.size - shared.size, 2U * config.memory.rx_buffer_size);
    for (bool share : {true, false}) {
        routes[1].phy = share ? &phys[0] : &phys[1];
        routes[2].phy = share ? &phys[0] : &phys[2];
        const size_t size = share ? shared.size : separate.size;
        void* storage = std::malloc(size);
        ASSERT_NE(storage, nullptr);
        xgl_handle_t handle = nullptr;
        EXPECT_EQ(xgl_init_static(&config, storage, size, &handle), XGL_OK);
        if (handle != nullptr) {
            xgl_work_budget_t budget = {128U, 100U};
            EXPECT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);
            xgl_destroy(handle);
        }
        std::free(storage);
    }
}

TEST(XglFootprintTest, MemoryReportExplainsEveryReservedByte) {
    xgl_phy_ops_t phy = {null_tx, null_rx, nullptr};
    xgl_route_item_t route = {2U, &phy, 128U, 100U, 1U};
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    config.route_table = &route;
    config.route_table_len = 1U;
    xgl_memory_report_t report{};
    ASSERT_EQ(xgl_memory_report(&config, &report), XGL_OK);
    const size_t explained = report.management_bytes +
                             report.initialization_bytes + report.peer_bytes +
                             report.window_bytes + report.tx_record_bytes +
                             report.tx_payload_bytes + report.scratch_bytes +
                             report.rx_buffered_bytes;
    EXPECT_EQ(explained, report.requirements.size);
    EXPECT_GT(report.management_bytes, 0U);
    EXPECT_GT(report.initialization_bytes, 0U);
    EXPECT_EQ(report.scratch_bytes, 128U);
    EXPECT_EQ(report.rx_buffered_bytes, 0U);
    void* workspace = std::malloc(report.requirements.size);
    ASSERT_NE(workspace, nullptr);
    xgl_handle_t handle = nullptr;
    EXPECT_EQ(
        xgl_init_static(&config, workspace, report.requirements.size, &handle),
        XGL_OK);
    xgl_destroy(handle);
    std::free(workspace);
}

TEST(XglFootprintTest, MemoryReportFailureDoesNotOverwriteOutput) {
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    xgl_memory_report_t report;
    std::memset(&report, 0x5a, sizeof(report));
    xgl_memory_report_t original;
    std::memcpy(&original, &report, sizeof(report));
    EXPECT_EQ(xgl_memory_report(nullptr, &report), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_memory_report(&config, nullptr), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_memory_report_checked(&config, sizeof(config),
                                        XGL_CONFIG_ABI_VERSION,
                                        XGL_BUILD_CONFIG_ID + 1U, &report),
              XGL_ERR_INVALID_VERSION);
    config.features.max_peers = 0U;
    EXPECT_EQ(xgl_memory_report(&config, &report), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(std::memcmp(&report, &original, sizeof(report)), 0);
}
