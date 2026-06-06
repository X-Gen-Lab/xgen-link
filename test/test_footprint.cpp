/**
 * \file            test_footprint.cpp
 * \brief           Footprint and allocation accounting tests
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <cstdlib>

namespace {

struct CountingAllocator {
    size_t allocations = 0;
    size_t frees = 0;
    size_t bytes = 0;
};

static CountingAllocator* g_allocator = nullptr;

static void* counting_malloc(size_t size) {
    void* ptr = std::malloc(size);
    if (g_allocator != nullptr && ptr != nullptr) {
        g_allocator->allocations++;
        g_allocator->bytes += size;
    }
    return ptr;
}

static void counting_free(void* ptr) {
    if (g_allocator != nullptr && ptr != nullptr) {
        g_allocator->frees++;
    }
    std::free(ptr);
}

static xgl_error_t null_tx(const uint8_t* data, size_t len, void* user_data) {
    (void)data;
    (void)len;
    (void)user_data;
    return XGL_OK;
}

static xgl_error_t null_rx(uint8_t* buffer, size_t* len, void* user_data) {
    (void)buffer;
    (void)user_data;
    if (len == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    *len = 0;
    return XGL_OK;
}

}  // namespace

TEST(XglFootprintTest, TinyPresetInitDestroyAllocationsAreBalanced) {
    CountingAllocator counter;
    g_allocator = &counter;

    xgl_allocator_t allocator = {
        .malloc = counting_malloc,
        .free = counting_free,
        .user_data = nullptr
    };
    xgl_phy_ops_t phy = {
        .tx = null_tx,
        .rx = null_rx,
        .user_data = nullptr
    };
    xgl_route_item_t routes[] = {
        { .target_id = 2, .phy = &phy, .max_frame_size = 128, .read_freq_hz = 100, .metric = 1 }
    };

    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    config.source_id = 1;
    config.memory.allocator = &allocator;
    config.route_table = routes;
    config.route_table_len = 1;

    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(xgl_init(handle), XGL_OK);

    EXPECT_GT(counter.allocations, 0U);
    EXPECT_GT(counter.bytes, config.memory.tx_pool_size);

    xgl_destroy(handle);

    EXPECT_EQ(counter.allocations, counter.frees);
    g_allocator = nullptr;
}
