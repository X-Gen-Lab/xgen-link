/**
 * \file            test_network_routes_contracts.cpp
 * \brief           Route configuration and independent PHY scheduling contracts
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include <api/xgl_instance_internal.h>
#include <network/xgl_route.h>
#include <xgen/memory/libc_allocator.h>
#include <xgl/xgl.h>

namespace {

struct RoutePhy {
    size_t reads = 0U;
};

xgl_error_t route_tx(const uint8_t*, size_t, void*) {
    return XGL_OK;
}

xgl_error_t route_rx(uint8_t*, size_t* len, void* user_data) {
    ++static_cast<RoutePhy*>(user_data)->reads;
    *len = 0U;
    return XGL_OK;
}

xgl_config_t route_config(xgl_route_item_t* routes, size_t count) {
    xgl_config_t config = {};
    xgl_config_get_preset_boot(&config);
    config.memory.allocator = xgm_allocator_libc();
    config.route_table = routes;
    config.route_table_len = count;
    return config;
}

using Instance = std::unique_ptr<xgl_instance, decltype(&xgl_destroy)>;

TEST(XglNetworkRoutesContract, ReservedAndLocalTargetsFailBeforeWorkspaceUse) {
    RoutePhy state;
    xgl_phy_ops_t phy = {route_tx, route_rx, &state};
    xgl_route_item_t route = {2U, &phy, 128U, 100U, 0U};
    auto config = route_config(&route, 1U);
    xgl_memory_requirements_t requirements = {};
    ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);
    std::unique_ptr<void, decltype(&std::free)> storage(
        std::malloc(requirements.size), &std::free);
    ASSERT_NE(storage, nullptr);

    for (uint16_t target : {uint16_t{0U}, config.source_id}) {
        SCOPED_TRACE(target);
        route.target_id = target;
        std::memset(storage.get(), 0xA5, requirements.size);
        const std::vector<uint8_t> before(requirements.size, 0xA5);
        xgl_memory_requirements_t unchanged = requirements;
        EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
        EXPECT_EQ(xgl_memory_requirements(&config, &unchanged),
                  XGL_ERR_INVALID_PARAM);
        EXPECT_EQ(std::memcmp(&unchanged, &requirements, sizeof(unchanged)), 0);
        xgl_handle_t handle = nullptr;
        EXPECT_EQ(
            xgl_init_static(&config, storage.get(), requirements.size, &handle),
            XGL_ERR_INVALID_PARAM);
        EXPECT_EQ(handle, nullptr);
        if (handle != nullptr) {
            xgl_destroy(handle);
        }
        EXPECT_EQ(std::memcmp(storage.get(), before.data(), before.size()), 0);
        Instance owned(xgl_create(&config), &xgl_destroy);
        EXPECT_EQ(owned, nullptr);
    }
    EXPECT_EQ(state.reads, 0U);
}

TEST(XglNetworkRoutesContract, BroadcastRemainsAnExplicitEgressDestination) {
    RoutePhy state;
    xgl_phy_ops_t phy = {route_tx, route_rx, &state};
    xgl_route_item_t route = {UINT16_MAX, &phy, 128U, 100U, 255U};
    auto config = route_config(&route, 1U);
    ASSERT_EQ(xgl_config_validate(&config), XGL_OK);
    Instance instance(xgl_create(&config), &xgl_destroy);
    ASSERT_NE(instance, nullptr);
    ASSERT_EQ(xgl_init(instance.get()), XGL_OK);
    const auto* selected =
        xgl_route_table_lookup(&instance->route_table, UINT16_MAX);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->phy, &phy);
    EXPECT_EQ(instance->link_count, 1U);
}

TEST(XglNetworkRoutesContract, RuntimeRouteUpdatesLeaveRegisteredLinksIntact) {
    RoutePhy original_state, replacement_state;
    xgl_phy_ops_t original = {route_tx, route_rx, &original_state};
    xgl_phy_ops_t replacement = {route_tx, route_rx, &replacement_state};
    xgl_route_item_t route = {2U, &original, 128U, 100U, 1U};
    auto config = route_config(&route, 1U);
    Instance instance(xgl_create(&config), &xgl_destroy);
    ASSERT_NE(instance, nullptr);
    ASSERT_EQ(xgl_init(instance.get()), XGL_OK);
    ASSERT_EQ(xgl_route_table_add(&instance->route_table, 2U, &replacement, 64U,
                                  1000U, 0U),
              XGL_OK);
    ASSERT_EQ(xgl_route_table_update_metric(&instance->route_table, 2U, 255U),
              XGL_OK);
    const auto* selected = xgl_route_table_lookup(&instance->route_table, 2U);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->phy, &replacement);
    EXPECT_EQ(selected->max_frame_size, 64U);
    EXPECT_EQ(selected->metric, 255U);
    ASSERT_EQ(instance->link_count, 1U);
    EXPECT_EQ(instance->links[0].phy, &original);
    EXPECT_EQ(instance->links[0].poll_interval_ms, 10U);

    const xgl_work_budget_t budget = {64U, 1000U};
    EXPECT_EQ(xgl_step(instance.get(), 0U, &budget), XGL_OK);
    EXPECT_EQ(xgl_step(instance.get(), 1U, &budget), XGL_OK);
    EXPECT_EQ(original_state.reads, 1U);
    EXPECT_EQ(replacement_state.reads, 0U);
    EXPECT_EQ(xgl_step(instance.get(), 10U, &budget), XGL_OK);
    EXPECT_EQ(original_state.reads, 2U);
    EXPECT_EQ(replacement_state.reads, 0U);
    EXPECT_EQ(route.phy, &original);
    EXPECT_EQ(route.read_freq_hz, 100U);
}

TEST(XglNetworkRoutesContract, InvalidLoadedSetPreservesExistingRoute) {
    RoutePhy state;
    xgl_phy_ops_t phy = {route_tx, route_rx, &state};
    xgl_route_table_t table = {};
    ASSERT_EQ(xgl_route_table_init(&table, 2U, xgm_allocator_libc()), XGL_OK);
    ASSERT_EQ(xgl_route_table_add(&table, 9U, &phy, 128U, 100U, 1U), XGL_OK);
    const std::array<std::array<uint16_t, 2>, 2> invalid_targets = {
        {{{2U, 2U}}, {{2U, 0U}}}};
    for (const auto& targets : invalid_targets) {
        SCOPED_TRACE(targets[1]);
        xgl_route_item_t routes[] = {{targets[0], &phy, 128U, 100U, 1U},
                                     {targets[1], &phy, 128U, 100U, 1U}};
        EXPECT_EQ(xgl_route_table_load(&table, routes, 2U),
                  XGL_ERR_INVALID_PARAM);
        EXPECT_EQ(xgl_route_table_count(&table), 1U);
        EXPECT_NE(xgl_route_table_lookup(&table, 9U), nullptr);
        EXPECT_EQ(xgl_route_table_lookup(&table, 2U), nullptr);
    }
    xgl_route_table_destroy(&table);
}

struct LinkAllocator {
    size_t calls = 0U;
    size_t fail_at = 0U;
    std::vector<void*> live;

    static void* allocate(void* ctx, size_t size) {
        auto& self = *static_cast<LinkAllocator*>(ctx);
        if (++self.calls == self.fail_at) {
            return nullptr;
        }
        void* storage = std::malloc(size);
        if (storage != nullptr) {
            self.live.push_back(storage);
        }
        return storage;
    }

    static void release(void* ctx, void* storage) {
        auto& self = *static_cast<LinkAllocator*>(ctx);
        if (storage != nullptr) {
            const auto found =
                std::find(self.live.begin(), self.live.end(), storage);
            EXPECT_NE(found, self.live.end());
            if (found != self.live.end()) {
                self.live.erase(found);
                std::free(storage);
            }
        }
    }
};

TEST(XglNetworkRoutesContract, LinkAllocationFailureReleasesPartialSetup) {
    RoutePhy first_state, second_state;
    xgl_phy_ops_t first = {route_tx, route_rx, &first_state};
    xgl_phy_ops_t second = {route_tx, route_rx, &second_state};
    xgl_route_item_t routes[] = {{2U, &first, 128U, 100U, 0U},
                                 {3U, &second, 128U, 50U, 0U}};
    auto config = route_config(routes, 2U);
    for (size_t failure : {1U, 2U, 3U}) {
        SCOPED_TRACE(failure);
        LinkAllocator state;
        state.fail_at = failure;
        xgm_allocator_t allocator = {};
        allocator.ctx = &state;
        allocator.alloc = LinkAllocator::allocate;
        allocator.free = LinkAllocator::release;
        xgl_instance instance = {};
        instance.config = &config;
        instance.allocator = &allocator;
        EXPECT_EQ(xgl_instance_init_links(&instance), XGL_ERR_NO_MEMORY);
        EXPECT_EQ(instance.links, nullptr);
        EXPECT_EQ(instance.link_count, 0U);
        EXPECT_TRUE(state.live.empty());
        state.fail_at = 0U;
        ASSERT_EQ(xgl_instance_init_links(&instance), XGL_OK);
        EXPECT_EQ(instance.link_count, 2U);
        xgl_instance_destroy_links(&instance);
        EXPECT_TRUE(state.live.empty());
    }
}

#if XGL_FEATURE_OUT_OF_ORDER
TEST(XglNetworkRoutesContract, DuplicateTargetsFailWithoutWorkspaceMutation) {
    RoutePhy first_state, second_state;
    xgl_phy_ops_t first = {route_tx, route_rx, &first_state};
    xgl_phy_ops_t second = {route_tx, route_rx, &second_state};
    xgl_route_item_t routes[] = {{2U, &first, 128U, 100U, 1U},
                                 {3U, &second, 128U, 50U, 0U}};
    auto config = route_config(routes, 2U);
    xgl_memory_requirements_t requirements = {};
    ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);
    std::unique_ptr<void, decltype(&std::free)> storage(
        std::malloc(requirements.size), &std::free);
    ASSERT_NE(storage, nullptr);
    routes[1].target_id = 2U;

    for (xgl_phy_ops_t* other_phy : {&first, &second}) {
        routes[1].phy = other_phy;
        std::memset(storage.get(), 0xA5, requirements.size);
        const std::vector<uint8_t> before(requirements.size, 0xA5);
        EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
        xgl_handle_t handle = nullptr;
        EXPECT_EQ(
            xgl_init_static(&config, storage.get(), requirements.size, &handle),
            XGL_ERR_INVALID_PARAM);
        EXPECT_EQ(handle, nullptr);
        if (handle != nullptr) {
            xgl_destroy(handle);
        }
        EXPECT_EQ(std::memcmp(storage.get(), before.data(), before.size()), 0);
    }
}

TEST(XglNetworkRoutesContract, SharedPhyPlanUsesFastestFrequencyInEitherOrder) {
    for (bool fast_first : {false, true}) {
        SCOPED_TRACE(fast_first);
        RoutePhy first_state, second_state;
        xgl_phy_ops_t first = {route_tx, route_rx, &first_state};
        xgl_phy_ops_t second = {route_tx, route_rx, &second_state};
        xgl_route_item_t routes[] = {
            {2U, &first, 128U, fast_first ? 500U : 100U, 1U},
            {3U, &second, 128U, 50U, 1U},
            {4U, &first, 128U, fast_first ? 100U : 500U, 0U}};
        auto config = route_config(routes, 3U);
        EXPECT_EQ(xgl_instance_count_links(&config), 2U);
        xgl_memory_requirements_t requirements = {};
        ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);
        std::unique_ptr<void, decltype(&std::free)> storage(
            std::malloc(requirements.size), &std::free);
        ASSERT_NE(storage, nullptr);
        xgl_handle_t handle = nullptr;
        ASSERT_EQ(
            xgl_init_static(&config, storage.get(), requirements.size, &handle),
            XGL_OK);
        Instance instance(handle, &xgl_destroy);
        ASSERT_EQ(instance->link_count, 2U);
        EXPECT_EQ(instance->links[0].phy, &first);
        EXPECT_EQ(instance->links[0].poll_interval_ms, 2U);
        EXPECT_EQ(instance->links[1].phy, &second);
        EXPECT_EQ(instance->links[1].poll_interval_ms, 20U);
        EXPECT_NE(instance->links[0].parser.cache,
                  instance->links[1].parser.cache);
        const xgl_work_budget_t budget = {64U, 1000U};
        EXPECT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);
        EXPECT_EQ(xgl_step(handle, 1U, &budget), XGL_OK);
        EXPECT_EQ(first_state.reads, 1U);
        EXPECT_EQ(second_state.reads, 1U);
        EXPECT_EQ(xgl_step(handle, 2U, &budget), XGL_OK);
        EXPECT_EQ(first_state.reads, 2U);
        EXPECT_EQ(second_state.reads, 1U);
        EXPECT_EQ(xgl_step(handle, 20U, &budget), XGL_OK);
        EXPECT_EQ(first_state.reads, 3U);
        EXPECT_EQ(second_state.reads, 2U);
    }
}

TEST(XglNetworkRoutesContract, SharedPhyHighFrequencyHasOneMillisecondMinimum) {
    for (uint32_t frequency : {1001U, UINT32_MAX}) {
        SCOPED_TRACE(frequency);
        for (bool fast_first : {false, true}) {
            SCOPED_TRACE(fast_first);
            RoutePhy first_state, second_state;
            xgl_phy_ops_t first = {route_tx, route_rx, &first_state};
            xgl_phy_ops_t second = {route_tx, route_rx, &second_state};
            xgl_route_item_t routes[] = {
                {2U, &first, 128U, fast_first ? frequency : 100U, 1U},
                {3U, &second, 128U, 50U, 1U},
                {4U, &first, 128U, fast_first ? 100U : frequency, 0U}};
            auto config = route_config(routes, 3U);
            Instance instance(xgl_create(&config), &xgl_destroy);
            ASSERT_NE(instance, nullptr);
            ASSERT_EQ(xgl_init(instance.get()), XGL_OK);
            ASSERT_EQ(instance->link_count, 2U);
            EXPECT_EQ(instance->links[0].phy, &first);
            EXPECT_EQ(instance->links[0].poll_interval_ms, 1U);
            EXPECT_EQ(instance->links[1].phy, &second);
            EXPECT_EQ(instance->links[1].poll_interval_ms, 20U);

            const xgl_work_budget_t budget = {64U, 1000U};
            EXPECT_EQ(xgl_step(instance.get(), 0U, &budget), XGL_OK);
            EXPECT_EQ(xgl_step(instance.get(), 0U, &budget), XGL_OK);
            EXPECT_EQ(first_state.reads, 1U);
            EXPECT_EQ(second_state.reads, 1U);
            EXPECT_EQ(xgl_step(instance.get(), 1U, &budget), XGL_OK);
            EXPECT_EQ(first_state.reads, 2U);
            EXPECT_EQ(second_state.reads, 1U);
            EXPECT_EQ(xgl_step(instance.get(), 19U, &budget), XGL_OK);
            EXPECT_EQ(first_state.reads, 3U);
            EXPECT_EQ(second_state.reads, 1U);
            EXPECT_EQ(xgl_step(instance.get(), 20U, &budget), XGL_OK);
            EXPECT_EQ(first_state.reads, 4U);
            EXPECT_EQ(second_state.reads, 2U);
        }
    }
}
#endif

}  // namespace
