/**
 * \file            test_coverage_api.cpp
 * \brief           Public lifecycle and bounded workspace edge contracts
 */

#include <gtest/gtest.h>
#include <xgen/memory/libc_allocator.h>
#include <xgl/xgl.h>

#include "api/xgl_instance_internal.h"
#include "test_security_helpers.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {

xgl_error_t coverage_sign(const xgl_auth_input_t*, uint8_t* tag,
                          size_t capacity, size_t* length, void*) {
    if (capacity < 4U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    std::memset(tag, 0x5A, 4);
    *length = 4;
    return XGL_OK;
}

xgl_error_t coverage_verify(const xgl_auth_input_t*, const uint8_t*, size_t,
                            bool* valid, void*) {
    *valid = true;
    return XGL_OK;
}

xgl_auth_provider_t coverage_provider() {
    xgl_auth_provider_t provider = {};
    provider.sign = coverage_sign;
    provider.verify = coverage_verify;
    provider.tag_len = 4;
    return provider;
}

xgl_config_t coverage_config() {
    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    config.memory.allocator = xgm_allocator_libc();
    return config;
}

struct CoveragePhy {
    unsigned reads = 0;
    unsigned writes = 0;
    xgl_error_t read_error = XGL_OK;
    xgl_error_t write_error = XGL_OK;
};

xgl_error_t coverage_tx(const uint8_t*, size_t, void* data) {
    auto* phy = static_cast<CoveragePhy*>(data);
    ++phy->writes;
    return phy->write_error;
}

xgl_error_t coverage_rx(uint8_t*, size_t* length, void* data) {
    auto* phy = static_cast<CoveragePhy*>(data);
    ++phy->reads;
    *length = 0;
    return phy->read_error;
}

xgl_phy_ops_t coverage_phy(CoveragePhy* state) {
    xgl_phy_ops_t phy = {};
    phy.tx = coverage_tx;
    phy.rx = coverage_rx;
    phy.user_data = state;
    return phy;
}

struct AllocationObserver {
    const xgm_allocator_t* backing;
    xgm_allocator_t service = {};
    std::vector<void*> live;
    size_t allocations = 0;
    size_t releases = 0;
    size_t duplicate_releases = 0;

    explicit AllocationObserver(const xgm_allocator_t* allocator)
        : backing(allocator) {
        service.ctx = this;
        service.alloc = [](void* context, size_t size) -> void* {
            auto& observer = *static_cast<AllocationObserver*>(context);
            void* result = xgm_alloc(observer.backing, size);
            if (result != nullptr) {
                observer.live.push_back(result);
                ++observer.allocations;
            }
            return result;
        };
        service.free = [](void* context, void* pointer) {
            auto& observer = *static_cast<AllocationObserver*>(context);
            auto found =
                std::find(observer.live.begin(), observer.live.end(), pointer);
            if (found == observer.live.end()) {
                ++observer.duplicate_releases;
                return;
            }
            observer.live.erase(found);
            ++observer.releases;
            xgm_free(observer.backing, pointer);
        };
    }
};

}  // namespace

TEST(XglCoverageApi,
     ConfigPresetsAcceptNullAndRejectIncompleteResourceBudgets) {
    xgl_config_get_default(nullptr);
    xgl_config_get_preset_boot(nullptr);
    xgl_config_get_preset_tiny(nullptr);
    xgl_config_get_preset_small(nullptr);
    xgl_config_get_preset_medium(nullptr);
    xgl_config_get_preset_large(nullptr);
    xgl_config_get_preset_production(nullptr);
    auto config = coverage_config();
    config.source_id = XGL_BROADCAST_ID;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
    config = coverage_config();
    config.features.max_peers = 0;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
    config = coverage_config();
    config.features.peer_idle_timeout_ms = UINT32_C(0x80000000);
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
    config = coverage_config();
    config.protocol.window_size = 2;
    config.features.max_rx_buffered_packets = 0;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
    for (int missing = 0; missing < 4; ++missing) {
        SCOPED_TRACE(missing);
        xgl_config_get_preset_small(&config);
        if (missing == 0) {
            config.features.max_message_size = 0;
        }
        if (missing == 1) {
            config.features.max_reassembly_slots = 0;
        }
        if (missing == 2) {
            config.features.max_reassembly_bytes = 0;
        }
        if (missing == 3) {
            config.features.max_tx_message_bytes = 0;
        }
        EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
    }
    auto provider = coverage_provider();
    config = coverage_config();
    config.auth_required = true;
    config.auth_provider = &provider;
    provider.sign = nullptr;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
    provider = coverage_provider();
    provider.verify = nullptr;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

TEST(XglCoverageApi,
     WorkspaceRejectsInsufficientMisalignedAndIncompatibleStorage) {
    auto config = coverage_config();
    xgl_memory_requirements_t required = {};
    ASSERT_EQ(xgl_memory_requirements(&config, &required), XGL_OK);
    EXPECT_EQ(xgl_memory_requirements(&config, nullptr), XGL_ERR_NULL_POINTER);
    std::vector<std::max_align_t> storage(
        (required.size + sizeof(std::max_align_t) - 1) /
            sizeof(std::max_align_t) +
        1);
    xgl_handle_t handle = nullptr;
    EXPECT_EQ(xgl_workspace_prepare(&config, storage.data(), required.size,
                                    nullptr, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_workspace_prepare(&config, nullptr, required.size, nullptr,
                                    &handle),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_workspace_prepare(&config, storage.data(), required.size - 1,
                                    nullptr, &handle),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(handle, nullptr);
    EXPECT_EQ(xgl_workspace_prepare(
                  &config, reinterpret_cast<uint8_t*>(storage.data()) + 1,
                  required.size, nullptr, &handle),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_init_static(&config, storage.data(), required.size, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_init_static_checked(
                  &config, sizeof(config) - 1, XGL_CONFIG_ABI_VERSION,
                  XGL_BUILD_CONFIG_ID, storage.data(), required.size, &handle),
              XGL_ERR_INVALID_VERSION);
    EXPECT_EQ(xgl_init_static_checked(
                  &config, sizeof(config), XGL_CONFIG_ABI_VERSION + 1,
                  XGL_BUILD_CONFIG_ID, storage.data(), required.size, &handle),
              XGL_ERR_INVALID_VERSION);
    EXPECT_EQ(xgl_init_static_checked(&config, sizeof(config),
                                      XGL_CONFIG_ABI_VERSION,
                                      XGL_BUILD_CONFIG_ID + 1, storage.data(),
                                      required.size, &handle),
              XGL_ERR_INVALID_VERSION);
    ASSERT_EQ(xgl_init_static(&config, storage.data(), required.size, &handle),
              XGL_OK);
    ASSERT_NE(handle, nullptr);
    xgl_destroy(handle);
}

TEST(XglCoverageApi,
     WorkspaceMeasuresOverflowWithoutAllocatingOrChangingResult) {
    for (int resource = 0; resource < 4; ++resource) {
        SCOPED_TRACE(resource);
        auto config = coverage_config();
        if (resource == 0) {
            config.features.max_tx_packets = SIZE_MAX;
        }
        if (resource == 1) {
            config.features.max_rx_buffered_packets = SIZE_MAX;
        }
        if (resource >= 2) {
            config.features.enable_fragmentation = true;
            config.features.max_message_size =
                resource == 2 ? SIZE_MAX : SIZE_MAX / 2;
            config.features.max_reassembly_slots = 2;
            config.features.max_reassembly_bytes = SIZE_MAX;
            config.features.max_tx_message_bytes = SIZE_MAX;
        }
        xgl_memory_requirements_t required = {};
        required.size = 123;
        ASSERT_EQ(xgl_config_validate(&config), XGL_OK);
        EXPECT_EQ(xgl_memory_requirements(&config, &required),
                  XGL_ERR_INVALID_PARAM);
        EXPECT_EQ(required.size, 123U);
    }
}

TEST(XglCoverageApi,
     RuntimeAndSendRejectUninitializedOrInvalidWorkWithoutProgress) {
    auto config = coverage_config();
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    uint32_t delay = 91;
    xgl_work_budget_t budget = {};
    xgl_tx_data_t tx = {};
    xgl_tx_data_zerocopy_t zero = {};
    EXPECT_FALSE(xgl_next_timeout(nullptr, 0, &delay));
    EXPECT_FALSE(xgl_next_timeout(handle, 0, &delay));
    EXPECT_EQ(delay, 91U);
    EXPECT_EQ(xgl_step(nullptr, 0, &budget), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_step(handle, 0, nullptr), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_step(handle, 0, &budget), XGL_ERR_NOT_INITIALIZED);
    EXPECT_EQ(xgl_send_at(handle, &tx, 0), XGL_ERR_NOT_INITIALIZED);
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 0), XGL_ERR_NOT_INITIALIZED);
    ASSERT_EQ(xgl_init(handle), XGL_OK);
    EXPECT_FALSE(xgl_next_timeout(handle, 0, nullptr));
    budget.receive_timeout_ms = UINT32_C(0x80000000);
    EXPECT_EQ(xgl_step(handle, 0, &budget), XGL_ERR_INVALID_PARAM);
    tx.data_len = 1;
    EXPECT_EQ(xgl_send_at(handle, &tx, 0), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 0), XGL_ERR_NULL_POINTER);
    uint8_t buffer[64] = {};
    zero.buffer = buffer;
    zero.buffer_size = sizeof(buffer);
    zero.data_offset = XGL_WIRE_BASE_HEADER_SIZE;
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 0), XGL_ERR_INVALID_PARAM);
    zero.data_len = 41;
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 0), XGL_ERR_BUFFER_TOO_SMALL);
    zero.data_len = 40;
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 0), XGL_ERR_BUFFER_TOO_SMALL);
    zero.data_len = 1;
    zero.priority = 8;
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 0), XGL_ERR_INVALID_PARAM);
    zero.priority = 0;
    zero.target_id = 123;
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 0), XGL_ERR_ROUTE_NOT_FOUND);
    xgl_destroy(handle);
    EXPECT_NE(xgl_error_string(static_cast<xgl_error_t>(0x7FFF)), nullptr);
}

TEST(XglCoverageApi, RuntimeUsesFastestSharedPhyAndPreservesFirstReceiveError) {
    CoveragePhy first, second;
    auto first_ops = coverage_phy(&first);
    auto second_ops = coverage_phy(&second);
    xgl_route_item_t routes[3] = {};
    for (size_t i = 0; i < 3; ++i) {
        routes[i].target_id = static_cast<uint16_t>(i + 2);
        routes[i].max_frame_size = 128;
    }
    routes[0].phy = &first_ops;
    routes[0].read_freq_hz = 10;
    routes[1].phy = &first_ops;
    routes[1].read_freq_hz = 2000;
    routes[2].phy = &second_ops;
    routes[2].read_freq_hz = 20;
    auto config = coverage_config();
    config.route_table = routes;
    config.route_table_len = 3;
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(xgl_init(handle), XGL_OK);
    ASSERT_EQ(handle->link_count, 2U);
    EXPECT_EQ(handle->links[0].read_freq_hz, 2000U);
    EXPECT_EQ(handle->links[0].poll_interval_ms, 1U);
    uint32_t delay = 99;
    ASSERT_TRUE(xgl_next_timeout(handle, 0, &delay));
    EXPECT_EQ(delay, 0U);
    xgl_work_budget_t budget = {};
    budget.rx_bytes = 64;
    budget.receive_timeout_ms = 1000;
    first.read_error = XGL_ERR_TIMEOUT;
    second.read_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(xgl_step(handle, 0, &budget), XGL_ERR_TIMEOUT);
    EXPECT_EQ(first.reads, 1U);
    EXPECT_EQ(second.reads, 1U);
    ASSERT_TRUE(xgl_next_timeout(handle, 0, &delay));
    EXPECT_EQ(delay, 1U);
    first.read_error = XGL_OK;
    second.read_error = XGL_OK;
    EXPECT_EQ(xgl_step(handle, 1, &budget), XGL_OK);
    EXPECT_EQ(first.reads, 2U);
    EXPECT_EQ(second.reads, 1U);
    ASSERT_TRUE(xgl_next_timeout(handle, 60, &delay));
    EXPECT_EQ(delay, 0U);
    xgl_destroy(handle);
    xgl_instance_destroy_links(nullptr);
}

TEST(XglCoverageApi, InvalidAllocatorAndDriverConfigurationFailBeforeUse) {
    auto config = coverage_config();
    xgm_allocator_t allocator = {};
    config.memory.allocator = &allocator;
    EXPECT_EQ(xgl_create(&config), nullptr);
    CoveragePhy state;
    auto phy = coverage_phy(&state);
    xgl_route_item_t route = {};
    route.target_id = 2;
    route.phy = &phy;
    route.max_frame_size = 128;
    route.read_freq_hz = 10;
    config.memory.allocator = xgm_allocator_libc();
    config.route_table = &route;
    config.route_table_len = 1;
    phy.rx = nullptr;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
    phy = coverage_phy(&state);
    route.max_frame_size = 2049;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(state.reads, 0U);
    EXPECT_EQ(state.writes, 0U);
}

TEST(XglCoverageApi, ReplayRejectsAnotherConnectionWithoutConsumingSequence) {
    xgl_replay_window_t replay = {};
    ASSERT_EQ(xgl_replay_window_init(&replay, 2, 3, 4, 64), XGL_OK);
    EXPECT_EQ(xgl_replay_window_check(&replay, 2, 9, 4, 7), XGL_REPLAY_REJECT);
    EXPECT_FALSE(replay.has_largest);
    EXPECT_EQ(xgl_replay_window_check(&replay, 2, 3, 4, 7),
              XGL_REPLAY_ACCEPT_NEW);
}

#if XGL_FEATURE_AUTH
TEST(XglCoverageApi, FailedInitializationReleasesEachAllocationOnlyOnce) {
    AllocationObserver workspace(xgm_allocator_libc());
    auto config = coverage_config();
    auto provider = coverage_provider();
    provider.sign = nullptr;
    config.auth_provider = &provider;
    CoveragePhy state;
    auto phy = coverage_phy(&state);
    xgl_route_item_t route = {2U, &phy, 128U, 100U, 1U};
    config.route_table = &route;
    config.route_table_len = 1U;
    config.memory.allocator = &workspace.service;
    ASSERT_EQ(xgl_config_validate(&config), XGL_OK);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);

    /* Observe the actual workspace allocator without changing its results. */
    AllocationObserver initialization(handle->allocator);
    handle->allocator = &initialization.service;
    EXPECT_EQ(xgl_init(handle), XGL_ERR_INVALID_PARAM);
    EXPECT_FALSE(handle->initialized);
    EXPECT_GT(initialization.allocations, 0U);
    EXPECT_EQ(initialization.releases, initialization.allocations);
    EXPECT_TRUE(initialization.live.empty());
    EXPECT_EQ(handle->links, nullptr);
    EXPECT_EQ(handle->link_count, 0U);
    xgl_destroy(handle);
    EXPECT_EQ(initialization.duplicate_releases, 0U);
    EXPECT_EQ(workspace.allocations, 1U);
    EXPECT_EQ(workspace.releases, 1U);
    EXPECT_EQ(workspace.duplicate_releases, 0U);
    EXPECT_TRUE(workspace.live.empty());
}

TEST(XglCoverageApi,
     PublicSecurityLifecycleRejectsEarlyCallsAndReservesClosedScope) {
    auto config = coverage_config();
    auto provider = coverage_provider();
    config.auth_provider = &provider;
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    auto session = test_session_config(2, 3, 4);
    EXPECT_EQ(xgl_install_security_session(nullptr, &session),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_install_security_session(handle, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_install_security_session(handle, &session),
              XGL_ERR_NOT_INITIALIZED);
    EXPECT_EQ(xgl_close_security_session(nullptr, 2, 3, 4),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_close_security_session(handle, 2, 3, 4),
              XGL_ERR_NOT_INITIALIZED);
    EXPECT_EQ(xgl_close_peer(nullptr, 2, 3, 4), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_close_peer(handle, 2, 3, 4), XGL_ERR_NOT_INITIALIZED);
    ASSERT_EQ(xgl_init(handle), XGL_OK);
    EXPECT_EQ(xgl_close_security_session(handle, 2, 3, 4), XGL_ERR_NOT_FOUND);
    ASSERT_EQ(xgl_install_security_session(handle, &session), XGL_OK);
    EXPECT_EQ(xgl_close_peer(handle, 2, 3, 4), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_close_peer(handle, 8, 3, 4), XGL_ERR_NOT_FOUND);
    EXPECT_EQ(xgl_close_peer(handle, 2, 8, 4), XGL_ERR_NOT_FOUND);
    EXPECT_EQ(xgl_close_peer(handle, 2, 3, 8), XGL_ERR_NOT_FOUND);
    EXPECT_EQ(xgl_close_security_session(handle, 2, 3, 4), XGL_OK);
    EXPECT_EQ(xgl_close_security_session(handle, 2, 3, 4), XGL_ERR_NOT_FOUND);
    EXPECT_EQ(xgl_install_security_session(handle, &session),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_close_peer(handle, 2, 3, 4), XGL_ERR_NOT_FOUND);
    xgl_destroy(handle);
}

TEST(XglCoverageApi, SecurityProviderAndSessionValidationPreserveNonceDomains) {
    auto provider = coverage_provider();
    xgl_security_ctx_t ctx = {};
    auto session = test_session_config(2, 3, 4);
    EXPECT_EQ(xgl_security_init(nullptr, 1, false, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_init(&ctx, 1, true, nullptr), XGL_ERR_INVALID_PARAM);
    for (int invalid = 0; invalid < 4; ++invalid) {
        provider = coverage_provider();
        if (invalid == 0) {
            provider.sign = nullptr;
        }
        if (invalid == 1) {
            provider.verify = nullptr;
        }
        if (invalid == 2) {
            provider.tag_len = 0;
        }
        if (invalid == 3) {
            provider.tag_len = XGL_AUTH_TAG_MAX_LEN + 1;
        }
        EXPECT_EQ(xgl_security_init(&ctx, 1, false, &provider),
                  XGL_ERR_INVALID_PARAM);
    }
    provider = coverage_provider();
    EXPECT_EQ(xgl_security_session_install(nullptr, &session),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_session_install(&ctx, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_session_install(&ctx, &session),
              XGL_ERR_INVALID_PARAM);
    ASSERT_EQ(xgl_security_init(&ctx, 0, false, &provider), XGL_OK);
    EXPECT_EQ(xgl_security_session_install(&ctx, &session),
              XGL_ERR_INVALID_PARAM);
    ASSERT_EQ(xgl_security_init(&ctx, 1, false, &provider), XGL_OK);
    ctx.busy = true;
    EXPECT_EQ(xgl_security_session_install(&ctx, &session), XGL_ERR_BUSY);
    ctx.busy = false;
    session.remote_id = 0;
    EXPECT_EQ(xgl_security_session_install(&ctx, &session),
              XGL_ERR_INVALID_PARAM);
    session.remote_id = 1;
    EXPECT_EQ(xgl_security_session_install(&ctx, &session),
              XGL_ERR_INVALID_PARAM);
    session.remote_id = 2;
    session.tx_key_id = 11;
    session.rx_key_id = 12;
    ASSERT_EQ(xgl_security_session_install(&ctx, &session), XGL_OK);
    for (int overlap = 0; overlap < 4; ++overlap) {
        auto other = test_session_config(9, 10, 11, 99);
        if (overlap == 0) {
            other.tx_key_id = session.tx_key_id;
            other.tx_nonce_prefix = session.tx_nonce_prefix;
        }
        if (overlap == 1) {
            other.rx_key_id = session.tx_key_id;
            other.rx_nonce_prefix = session.tx_nonce_prefix;
        }
        if (overlap == 2) {
            other.tx_key_id = session.rx_key_id;
            other.tx_nonce_prefix = session.rx_nonce_prefix;
        }
        if (overlap == 3) {
            other.rx_key_id = session.rx_key_id;
            other.rx_nonce_prefix = session.rx_nonce_prefix;
        }
        EXPECT_EQ(xgl_security_session_install(&ctx, &other),
                  XGL_ERR_INVALID_PARAM);
    }
    auto other = test_session_config(2, 8, 4, 17);
    ASSERT_EQ(xgl_security_session_install(&ctx, &other), XGL_OK);
    other = test_session_config(2, 3, 8, 18);
    ASSERT_EQ(xgl_security_session_install(&ctx, &other), XGL_OK);
    EXPECT_EQ(xgl_security_session_close(nullptr, 2, 3, 4),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_session_close(&ctx, 2, 99, 4), XGL_ERR_NOT_FOUND);
    EXPECT_EQ(xgl_security_session_close(&ctx, 99, 3, 4), XGL_ERR_NOT_FOUND);
    ASSERT_EQ(xgl_security_session_close(&ctx, 2, 3, 4), XGL_OK);
    EXPECT_EQ(xgl_security_session_close(&ctx, 2, 3, 4), XGL_ERR_NOT_FOUND);
}

TEST(XglCoverageApi,
     SecuritySigningRejectsInvalidBuffersBeforeProviderInvocation) {
    auto provider = coverage_provider();
    xgl_security_ctx_t ctx = {};
    uint8_t bytes[128] = {};
    size_t written = 17;
    EXPECT_EQ(xgl_security_sign_frame(nullptr, bytes, 128, 24, 0, &written),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_sign_frame(&ctx, nullptr, 128, 24, 0, &written),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_sign_frame(&ctx, bytes, 128, 24, 0, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_sign_frame(&ctx, bytes, 128, 24, 0, &written),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(written, 0U);
    ASSERT_EQ(xgl_security_init(&ctx, 1, false, &provider), XGL_OK);
    ctx.busy = true;
    EXPECT_EQ(xgl_security_sign_frame(&ctx, bytes, 128, 24, 0, &written),
              XGL_ERR_BUSY);
    ctx.busy = false;
    EXPECT_EQ(xgl_security_sign_frame(&ctx, bytes, 24, 25, 0, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_security_sign_frame(&ctx, bytes, 24, 24, 1, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_security_sign_frame(&ctx, bytes, 24, 24, 0, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_security_sign_frame(&ctx, bytes, 128, 24, 0, &written),
              XGL_ERR_INVALID_FRAME);
    for (int invalid = 0; invalid < 6; ++invalid) {
        xgl_wire_header_t header = {};
        header.version = XGL_WIRE_VERSION;
        header.header_len = 24;
        header.packet_type = XGL_PACKET_TYPE_DATA;
        header.flags = XGL_WIRE_FLAG_AUTHENTICATED;
        header.source_id = 1;
        header.target_id = 2;
        if (invalid == 0) {
            header.header_len = 25;
        }
        if (invalid == 1) {
            header.payload_len = 1;
        }
        if (invalid == 2) {
            header.source_id = 3;
        }
        if (invalid == 3) {
            header.flags = 0;
        }
        if (invalid >= 4) {
            header.header_len = 39;
        }
        ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header),
                  XGL_OK);
        if (invalid == 4) {
            bytes[24] = 0;
        }
        if (invalid == 5) {
            bytes[24] = XGL_WIRE_EXT_SECURITY;
            bytes[25] = 13;
            bytes[38] = 3;
        }
        EXPECT_EQ(xgl_security_sign_frame(&ctx, bytes, 128,
                                          invalid >= 4 ? 39 : 24, 0, &written),
                  XGL_ERR_INVALID_FRAME);
        EXPECT_EQ(written, 0U);
    }
}

TEST(XglCoverageApi,
     SecurityVerificationRejectsUntrustedMetadataAndBusyContext) {
    xgl_security_ctx_t ctx = {};
    auto provider = coverage_provider();
    xgl_wire_frame_view_t view = {};
    EXPECT_EQ(xgl_security_verify_frame(nullptr, &view), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_verify_frame(&ctx, nullptr), XGL_ERR_NULL_POINTER);
    ASSERT_EQ(xgl_security_init(&ctx, 1, false, nullptr), XGL_OK);
    EXPECT_EQ(xgl_security_verify_frame(&ctx, &view), XGL_OK);
    ctx.busy = true;
    EXPECT_EQ(xgl_security_verify_frame(&ctx, &view), XGL_ERR_BUSY);
    ctx.busy = false;
    view.authenticated = true;
    view.header.target_id = 1;
    EXPECT_EQ(xgl_security_verify_frame(&ctx, &view), XGL_ERR_INVALID_FRAME);
    ASSERT_EQ(xgl_security_init(&ctx, 1, true, &provider), XGL_OK);
    EXPECT_EQ(xgl_security_verify_frame(&ctx, &view), XGL_ERR_INVALID_FRAME);
    view.has_security_ext = true;
    view.auth_tag_len = 3;
    EXPECT_EQ(xgl_security_verify_frame(&ctx, &view), XGL_ERR_INVALID_FRAME);
    view.auth_tag_len = 4;
    view.header.source_id = 2;
    auto session = test_session_config(2);
    ASSERT_EQ(xgl_security_session_install(&ctx, &session), XGL_OK);
    view.auth_key_id = session.rx_key_id + 1;
    EXPECT_EQ(xgl_security_verify_frame(&ctx, &view), XGL_ERR_INVALID_FRAME);
    view.auth_key_id = session.rx_key_id;
    ASSERT_EQ(xgl_security_session_close(&ctx, 2, 0, 0), XGL_OK);
    EXPECT_EQ(xgl_security_verify_frame(&ctx, &view), XGL_ERR_INVALID_FRAME);
}

TEST(XglCoverageApi, AuthenticatedFrameBuilderRejectsInvalidBorrowedInputs) {
    auto provider = coverage_provider();
    xgl_security_ctx_t ctx = {};
    ASSERT_EQ(xgl_security_init(&ctx, 1, false, &provider), XGL_OK);
    auto session = test_session_config(2);
    ASSERT_EQ(xgl_security_session_install(&ctx, &session), XGL_OK);
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = 1;
    params.target_id = 2;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    uint8_t bytes[300] = {}, extensions[15] = {};
    size_t written = 0;
    EXPECT_EQ(
        xgl_security_serialize_frame(nullptr, 300, &frame, &ctx, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, nullptr, &ctx, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_security_serialize_frame(bytes, 300, &frame, nullptr,
                                                &written),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, nullptr),
        XGL_ERR_NULL_POINTER);
    ctx.provider = nullptr;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_NULL_POINTER);
    ctx.provider = &provider;
    ctx.busy = true;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_BUSY);
    ctx.busy = false;
    frame.payload_len = 1;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_NULL_POINTER);
    frame.payload = bytes;
    frame.payload_len = UINT16_MAX + 1U;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_BUFFER_TOO_SMALL);
    frame.payload_len = 0;
    provider.tag_len = 0;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_INVALID_PARAM);
    provider.tag_len = XGL_AUTH_TAG_MAX_LEN + 1;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_INVALID_PARAM);
    provider.tag_len = 4;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 38, &frame, &ctx, &written),
        XGL_ERR_BUFFER_TOO_SMALL);
    frame.header.source_id = 0;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_INVALID_PARAM);
    frame.header.source_id = 1;
    frame.extensions = extensions;
    frame.extensions_len = sizeof(extensions);
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_INVALID_FRAME);
    extensions[0] = XGL_WIRE_EXT_SECURITY;
    extensions[1] = 13;
    extensions[14] = 4;
    EXPECT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_ERR_INVALID_FRAME);
    frame.extensions_len = 0;
    ASSERT_EQ(
        xgl_security_serialize_frame(bytes, 300, &frame, &ctx, &written),
        XGL_OK);
    EXPECT_EQ(written, 45U);
}

TEST(XglCoverageApi,
     AuthenticatedZeroCopySendsTypedPayloadAndClosesRetainedPeer) {
    auto provider = coverage_provider();
    CoveragePhy phy_state;
    auto phy = coverage_phy(&phy_state);
    xgl_route_item_t route = {};
    route.target_id = 2;
    route.phy = &phy;
    route.max_frame_size = 128;
    route.read_freq_hz = 10;
    auto config = coverage_config();
    config.route_table = &route;
    config.route_table_len = 1;
    config.auth_required = true;
    config.auth_provider = &provider;
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(xgl_init(handle), XGL_OK);
    auto session = test_session_config(2);
    ASSERT_EQ(xgl_install_security_session(handle, &session), XGL_OK);
    uint8_t buffer[128] = {};
    xgl_tx_data_zerocopy_t zero = {};
    zero.buffer = buffer;
    zero.buffer_size = sizeof(buffer);
    zero.data_len = 1;
    zero.target_id = 2;
    zero.data_type = 9;
    zero.data_offset = XGL_WIRE_BASE_HEADER_SIZE;
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 1), XGL_ERR_INVALID_PARAM);
    zero.data_offset += XGL_SECURITY_EXT_SIZE + XGL_DATA_TYPE_EXT_SIZE;
    buffer[zero.data_offset] = 0x73;
    ASSERT_EQ(xgl_send_zerocopy_at(handle, &zero, 1), XGL_OK);
    EXPECT_EQ(phy_state.writes, 1U);
    phy_state.write_error = XGL_ERR_TX_FAILED;
    EXPECT_EQ(xgl_send_zerocopy_at(handle, &zero, 1), XGL_ERR_TX_FAILED);
    phy_state.write_error = XGL_OK;
    xgl_tx_data_t tx = {};
    tx.target_id = 2;
    tx.data = buffer + zero.data_offset;
    tx.data_len = 1;
    tx.reliable = true;
    ASSERT_EQ(xgl_send_at(handle, &tx, 1), XGL_OK);
    EXPECT_EQ(xgl_close_security_session(handle, 2, 0, 0), XGL_OK);
    xgl_destroy(handle);
}
#endif
