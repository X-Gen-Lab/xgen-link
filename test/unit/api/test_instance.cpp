/**
 * \file            test_instance.cpp
 * \brief           Unit tests for protocol instance management
 * \author          X-Gen Lab
 */

#include "test_host_allocator.h"

#include <xgl/xgl.h>

#include <cstddef>
#include <cstdlib>
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>
#include <vector>

#include "api/xgl_instance_internal.h"

template <typename T, typename = void>
struct HasLegacySequenceState : std::false_type {};

template <typename T>
struct HasLegacySequenceState<
    T, std::void_t<decltype(std::declval<T>().seq_numbers),
                   decltype(std::declval<T>().seq_numbers_count)>>
    : std::true_type {};

static_assert(
    !HasLegacySequenceState<struct xgl_instance>::value,
    "xgl_instance must not allocate or retain legacy 8-bit sequence state");

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglInstanceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        /* Get default configuration */
        xgl_config_get_default(&config);
        config.source_id = 1;
    }

    void TearDown() override {
        /* Cleanup handled by individual tests */
    }

    xgl_config_t config;
};

/*---------------------------------------------------------------------------*/
/* Basic Instance Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance creation with valid configuration
 */
TEST_F(XglInstanceTest, CreateWithValidConfig) {
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);

    ASSERT_NE(handle, nullptr);

    xgl_destroy(handle);
}

/**
 * \brief           Test instance creation with NULL configuration
 */
TEST_F(XglInstanceTest, CreateWithNullConfig) {
    xgl_handle_t handle = xgl_create(NULL);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Reject creation without reliable packet capacity
 */
TEST_F(XglInstanceTest, CreateWithZeroTxPacketCapacity) {
    config.features.max_tx_packets = 0U;

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid RX buffer size
 */
TEST_F(XglInstanceTest, CreateWithInvalidRxBufferSize) {
    config.memory.rx_buffer_size = 0;

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid ACK timeout
 */
TEST_F(XglInstanceTest, CreateWithInvalidAckTimeout) {
    config.protocol.ack_timeout_ms = 0;

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid max frame size
 */
TEST_F(XglInstanceTest, CreateWithInvalidMaxFrameSize) {
    config.protocol.max_frame_size = 0;

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with frame size too small
 */
TEST_F(XglInstanceTest, CreateWithFrameSizeTooSmall) {
    config.protocol.max_frame_size = 10; /* Less than header + CRC */

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);

    EXPECT_EQ(handle, nullptr);
}

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance initialization
 */
TEST_F(XglInstanceTest, InitializeInstance) {
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);

    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);

    xgl_destroy(handle);
}

/**
 * \brief           Test initialization with NULL handle
 */
TEST_F(XglInstanceTest, InitializeNullHandle) {
    xgl_error_t err = xgl_init(NULL);

    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/**
 * \brief           Test double initialization
 */
TEST_F(XglInstanceTest, DoubleInitialization) {
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);

    xgl_error_t err1 = xgl_init(handle);
    EXPECT_EQ(err1, XGL_OK);

    xgl_error_t err2 = xgl_init(handle);
    EXPECT_EQ(err2, XGL_ERR_ALREADY_INITIALIZED);

    xgl_destroy(handle);
}

/*---------------------------------------------------------------------------*/
/* Destruction Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance destruction
 */
TEST_F(XglInstanceTest, DestroyInstance) {
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);

    xgl_error_t err = xgl_init(handle);
    ASSERT_EQ(err, XGL_OK);

    /* Should not crash */
    xgl_destroy(handle);
}

/**
 * \brief           Test destruction with NULL handle
 */
TEST_F(XglInstanceTest, DestroyNullHandle) {
    /* Should not crash */
    xgl_destroy(NULL);
}

/**
 * \brief           Test destruction without initialization
 */
TEST_F(XglInstanceTest, DestroyWithoutInit) {
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);

    /* Should not crash even if not initialized */
    xgl_destroy(handle);
}

/*---------------------------------------------------------------------------*/
/* Configuration Preset Tests                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test tiny configuration preset
 */
TEST_F(XglInstanceTest, TinyConfigPreset) {
    xgl_config_t tiny_config = XGL_CONFIG_PRESET_TINY;
    tiny_config.source_id = 1;

    xgl_test_use_host_allocator(&tiny_config);
    xgl_handle_t handle = xgl_create(&tiny_config);
    ASSERT_NE(handle, nullptr);

    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);

    xgl_destroy(handle);
}

/**
 * \brief           Test small configuration preset
 */
TEST_F(XglInstanceTest, SmallConfigPreset) {
    xgl_config_t small_config = XGL_CONFIG_PRESET_SMALL;
    small_config.source_id = 1;

    xgl_test_use_host_allocator(&small_config);
    xgl_handle_t handle = xgl_create(&small_config);
    ASSERT_NE(handle, nullptr);

    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);

    xgl_destroy(handle);
}

/**
 * \brief           Test medium configuration preset
 */
TEST_F(XglInstanceTest, MediumConfigPreset) {
    xgl_config_t medium_config = XGL_CONFIG_PRESET_MEDIUM;
    medium_config.source_id = 1;

    xgl_test_use_host_allocator(&medium_config);
    xgl_handle_t handle = xgl_create(&medium_config);
    ASSERT_NE(handle, nullptr);

    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);

    xgl_destroy(handle);
}

/**
 * \brief           Test large configuration preset
 */
TEST_F(XglInstanceTest, LargeConfigPreset) {
    xgl_config_t large_config = XGL_CONFIG_PRESET_LARGE;
    large_config.source_id = 1;

    xgl_test_use_host_allocator(&large_config);
    xgl_handle_t handle = xgl_create(&large_config);
    ASSERT_NE(handle, nullptr);

    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);

    xgl_destroy(handle);
}

/*---------------------------------------------------------------------------*/
/* Multiple Instance Tests                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test creating multiple instances
 */
TEST_F(XglInstanceTest, MultipleInstances) {
    xgl_config_t config1 = config;
    xgl_config_t config2 = config;

    config1.source_id = 1;
    config2.source_id = 2;

    xgl_test_use_host_allocator(&config1);
    xgl_handle_t handle1 = xgl_create(&config1);
    xgl_test_use_host_allocator(&config2);
    xgl_handle_t handle2 = xgl_create(&config2);

    ASSERT_NE(handle1, nullptr);
    ASSERT_NE(handle2, nullptr);
    EXPECT_NE(handle1, handle2);

    xgl_error_t err1 = xgl_init(handle1);
    xgl_error_t err2 = xgl_init(handle2);

    EXPECT_EQ(err1, XGL_OK);
    EXPECT_EQ(err2, XGL_OK);

    xgl_destroy(handle1);
    xgl_destroy(handle2);
}

TEST_F(XglInstanceTest, NextTimeoutReportsNoWorkWhenIdleWithoutRoutes) {
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);

    ASSERT_EQ(xgl_init(handle), XGL_OK);

    uint32_t delay = 17U;
    EXPECT_FALSE(xgl_next_timeout(handle, 0U, &delay));

    xgl_destroy(handle);
}

TEST_F(XglInstanceTest, NextTimeoutReportsRoutePollingAndReliableTimeouts) {
    xgl_phy_ops_t phy = {};
    phy.tx = [](const uint8_t* data, size_t len,
                void* user_data) -> xgl_error_t {
        (void)data;
        (void)len;
        (void)user_data;
        return XGL_OK;
    };
    phy.rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
        (void)buffer;
        (void)user_data;
        *len = 0;
        return XGL_OK;
    };
    phy.user_data = NULL;
    xgl_route_item_t routes[] = {{/* target_id */ 2,
                                  /* phy */ &phy,
                                  /* max_frame_size */ 256,
                                  /* read_freq_hz */ 1,
                                  /* metric */ 1}};

    config.route_table = routes;
    config.route_table_len = 1;
    config.protocol.ack_timeout_ms = 500;

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(xgl_init(handle), XGL_OK);

    uint32_t delay = 17U;
    ASSERT_TRUE(xgl_next_timeout(handle, 0U, &delay));
    EXPECT_EQ(delay, 0U);
    const xgl_work_budget_t budget{256U, 100U};
    ASSERT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);

    const uint8_t payload[] = {'d', 'a', 't', 'a'};
    xgl_tx_data_t tx_data = {};
    tx_data.target_id = 2;
    tx_data.data_type = 1;
    tx_data.data = payload;
    tx_data.data_len = sizeof(payload);
    tx_data.reliable = true;
    tx_data.priority = 0;
    tx_data.timeout_ms = 250;
    ASSERT_EQ(xgl_send_at(handle, &tx_data, 0U), XGL_OK);

    ASSERT_TRUE(xgl_next_timeout(handle, 0U, &delay));
    EXPECT_EQ(delay, 250U);

    xgl_destroy(handle);
}

/*---------------------------------------------------------------------------*/
/* Route Configuration Tests                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance with route table
 */
TEST_F(XglInstanceTest, InstanceWithRouteTable) {
    /* Create mock PHY operations */
    xgl_phy_ops_t phy = {};
    phy.tx = [](const uint8_t* data, size_t len,
                void* user_data) -> xgl_error_t {
        (void)data;
        (void)len;
        (void)user_data;
        return XGL_OK;
    };
    phy.rx = [](uint8_t* buffer, size_t* len, void* user_data) -> xgl_error_t {
        (void)buffer;
        (void)user_data;
        *len = 0; /* No data available */
        return XGL_OK;
    };
    phy.user_data = NULL;

    xgl_route_item_t routes[] = {{/* target_id */ 2,
                                  /* phy */ &phy,
                                  /* max_frame_size */ 256,
                                  /* read_freq_hz */ 100,
                                  /* metric */ 100},
                                 {/* target_id */ 3,
                                  /* phy */ &phy,
                                  /* max_frame_size */ 256,
                                  /* read_freq_hz */ 100,
                                  /* metric */ 100}};

    config.route_table = routes;
    config.route_table_len = 2;

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);

    xgl_error_t err = xgl_init(handle);
    EXPECT_EQ(err, XGL_OK);

    xgl_destroy(handle);
}

/**
 * \brief           Test instance with invalid route table
 */
TEST_F(XglInstanceTest, InstanceWithInvalidRouteTable) {
    config.route_table = NULL;
    config.route_table_len = 5; /* Non-zero length but NULL pointer */

    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);

    EXPECT_EQ(handle, nullptr);
}

TEST_F(XglInstanceTest, SharedPhyIsPolledOnceAndTimeZeroIsValid) {
    size_t reads = 0;
    xgl_phy_ops_t phy = {};
    phy.tx = [](const uint8_t*, size_t, void*) -> xgl_error_t {
        return XGL_OK;
    };
    phy.rx = [](uint8_t*, size_t* len, void* ctx) -> xgl_error_t {
        ++*static_cast<size_t*>(ctx);
        *len = 0;
        return XGL_OK;
    };
    phy.user_data = &reads;
    xgl_route_item_t routes[] = {{2, &phy, 128, 100, 0},
                                 {3, &phy, 128, 100, 0}};
    config.route_table = routes;
    config.route_table_len = 2;
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    ASSERT_EQ(xgl_init(handle), XGL_OK);
    xgl_work_budget_t budget{64, 1000};
    ASSERT_EQ(xgl_step(handle, 0, &budget), XGL_OK);
    EXPECT_EQ(reads, 1U);
    uint32_t delay = 0;
    ASSERT_TRUE(xgl_next_timeout(handle, 0, &delay));
    EXPECT_EQ(delay, 10U);
    EXPECT_EQ(xgl_step(handle, 9, &budget), XGL_OK);
    EXPECT_EQ(reads, 1U);
    EXPECT_EQ(xgl_step(handle, 10, &budget), XGL_OK);
    EXPECT_EQ(reads, 2U);
    xgl_destroy(handle);
}

/**
 * \brief           Reject mismatched consumer ABI before reading configuration
 */
TEST_F(XglInstanceTest, RejectsConsumerAbiAndProfileMismatch) {
    EXPECT_EQ(xgl_create_checked(nullptr, 1U, XGL_CONFIG_ABI_VERSION,
                                 XGL_BUILD_CONFIG_ID),
              nullptr);
    EXPECT_EQ(xgl_create_checked(&config, sizeof(config),
                                 XGL_CONFIG_ABI_VERSION,
                                 XGL_BUILD_CONFIG_ID + 1U),
              nullptr);
    xgl_memory_requirements_t requirements{};
    EXPECT_EQ(xgl_memory_requirements_checked(
                  nullptr, sizeof(config), XGL_CONFIG_ABI_VERSION - 1U,
                  XGL_BUILD_CONFIG_ID, &requirements),
              XGL_ERR_INVALID_VERSION);
    xgl_handle_t handle = nullptr;
    EXPECT_EQ(xgl_init_static_checked(nullptr, 1U, XGL_CONFIG_ABI_VERSION,
                                      XGL_BUILD_CONFIG_ID, nullptr, 0U,
                                      &handle),
              XGL_ERR_INVALID_VERSION);
}

/**
 * \brief           Reject impossible route counts before reading route storage
 */
TEST_F(XglInstanceTest, RejectsOversizedRouteCount) {
    xgl_route_item_t route{};
    config.route_table = &route;
    config.route_table_len = static_cast<size_t>(UINT16_MAX) + 1U;
    EXPECT_EQ(xgl_config_validate(&config), XGL_ERR_INVALID_PARAM);
}

/**
 * \brief           Reserve windows and bounded fragmentation in static storage
 */
TEST_F(XglInstanceTest, StaticWorkspaceSupportsWindowsAndFragmentation) {
    xgl_config_get_preset_boot(&config);
    xgl_memory_requirements_t requirements{};
    ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);
    config.protocol.window_size = 2U;
    config.features.max_rx_buffered_packets = 1U;
    ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);
    const size_t unfragmented = requirements.size;
    config.features.enable_fragmentation = true;
    config.features.max_message_size = 256U;
    config.features.max_reassembly_slots = 1U;
    config.features.max_reassembly_bytes = 256U;
    config.features.max_tx_message_bytes = 256U;
    ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);
    EXPECT_GT(requirements.size, unfragmented);
    std::vector<xgm_max_align_t> storage(
        (requirements.size + sizeof(xgm_max_align_t) - 1U) /
        sizeof(xgm_max_align_t));
    xgl_handle_t handle = nullptr;
    ASSERT_EQ(
        xgl_init_static(&config, storage.data(), requirements.size, &handle),
        XGL_OK);
    EXPECT_EQ(handle->config, &config);
    xgl_destroy(handle);
}

/**
 * \brief           Reserve once and release through the explicit owning backend
 */
TEST_F(XglInstanceTest, WorkspaceBackendFailureAndInitializationIsolation) {
    struct FailureAllocator {
        size_t calls = 0U;
        size_t fail_at = 0U;
        size_t live = 0U;
    } state;

    xgm_allocator_t service = {
        &state,
        [](void* ctx, size_t size) -> void* {
            auto* state = static_cast<FailureAllocator*>(ctx);
            if (++state->calls == state->fail_at) {
                return nullptr;
            }
            void* result = std::malloc(size);
            if (result != nullptr) {
                ++state->live;
            }
            return result;
        },
        [](void* ctx, void* ptr) {
            if (ptr != nullptr) {
                --static_cast<FailureAllocator*>(ctx)->live;
                std::free(ptr);
            }
        }};
    config.memory.allocator = &service;
    xgl_test_use_host_allocator(&config);
    xgl_handle_t baseline = xgl_create(&config);
    ASSERT_NE(baseline, nullptr);
    ASSERT_EQ(xgl_init(baseline), XGL_OK);
    const size_t allocation_count = state.calls;
    ASSERT_EQ(allocation_count, 1U);
    xgl_destroy(baseline);
    ASSERT_EQ(state.live, 0U);
    for (size_t failed = 1U; failed <= allocation_count; ++failed) {
        state = {0U, failed, 0U};
        xgl_test_use_host_allocator(&config);
        xgl_handle_t handle = xgl_create(&config);
        EXPECT_EQ(handle, nullptr);
        if (handle != nullptr) {
            xgl_destroy(handle);
        }
        EXPECT_EQ(state.live, 0U) << failed;
    }
    state = {0U, 2U, 0U};
    xgl_test_use_host_allocator(&config);
    xgl_handle_t handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->config, &config);
    EXPECT_EQ(xgl_init(handle), XGL_OK);
    EXPECT_EQ(state.calls, 1U);
    xgl_destroy(handle);
    EXPECT_EQ(state.live, 0U);
}
