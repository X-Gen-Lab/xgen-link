/**
 * \file            test_instance.cpp
 * \brief           Unit tests for protocol instance management
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_config.h"
#include "test_host_allocator.h"
#include <xgl/xgl_alloc.h>

#include <xgl/xgl.h>
#include <xgl/xgl_runtime.h>

#include <cstddef>
#include <cstdlib>
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>
#include <vector>

#include "api/xgl_instance_internal.h"

/** \brief           Exercise deadlines and failures through the public step. */
class XglRuntimeContractTest : public ::testing::Test {
  protected:
    struct CallbackContext {
        XglRuntimeContractTest& owner;
        bool receive;
    };

    void SetUp() override {
        config = xgl_test_config();
        config.source_id = 1U;
        config.protocol.max_frame_size = 128U;
        config.memory.rx_buffer_size = 128U;
        config.route_table = &route;
        config.route_table_len = 1U;
        config.rx_user_data = &receive_context;
        config.callback_user_data = &error_context;
        config.error_callback = [](xgl_handle_t, xgl_error_t error, const char*,
                                   void* user) {
            const auto& context = *static_cast<CallbackContext*>(user);
            EXPECT_FALSE(context.receive);
            if (error == XGL_ERR_TIMEOUT) {
                ++context.owner.timeouts;
            }
        };
        config.rx_callback = [](xgl_handle_t, const xgl_rx_info_t*,
                                const uint8_t*, size_t,
                                void* user) -> xgl_error_t {
            const auto& context = *static_cast<CallbackContext*>(user);
            EXPECT_TRUE(context.receive);
            auto& owner = context.owner;
            const size_t index = owner.deliveries++;
            return index < owner.results.size() ? owner.results[index] : XGL_OK;
        };
        phy.user_data = this;
        phy.tx = [](const uint8_t*, size_t, void* user) -> xgl_error_t {
            ++static_cast<XglRuntimeContractTest*>(user)->writes;
            return XGL_OK;
        };
        phy.rx = [](uint8_t* data, size_t* length, void* user) -> xgl_error_t {
            auto* self = static_cast<XglRuntimeContractTest*>(user);
            ++self->reads;
            size_t copied = 0U;
            while (copied < *length && self->cursor < self->input.size()) {
                data[copied++] = self->input[self->cursor++];
            }
            *length = copied;
            return XGL_OK;
        };
        handle = xgl_test_init_allocated(&config);
        ASSERT_NE(handle, nullptr);
    }

    void TearDown() override {
        xgl_destroy(handle);
    }

    void AppendFrame() {
        const uint8_t payload = 42U;
        xgl_frame_params_t params{};
        params.source_id = 2U;
        params.target_id = 1U;
        params.payload = &payload;
        params.payload_len = 1U;
        xgl_frame_t frame{};
        ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
        uint8_t bytes[128]{};
        size_t length = 0U;
        ASSERT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &length),
                  XGL_OK);
        input.insert(input.end(), bytes, bytes + length);
    }

    xgl_config_t config{};
    CallbackContext receive_context{*this, true};
    CallbackContext error_context{*this, false};
    xgl_phy_ops_t phy{};
    xgl_route_item_t route{2U, &phy, 128U, 1U};
    xgl_handle_t handle = nullptr;
    std::vector<uint8_t> input;
    std::vector<xgl_error_t> results;
    size_t cursor = 0U;
    size_t reads = 0U;
    size_t writes = 0U;
    size_t deliveries = 0U;
    size_t timeouts = 0U;
};

TEST_F(XglRuntimeContractTest, ParserDeadlinePrecedesSlowPhyPolling) {
    input = {XGL_WIRE_MAGIC_0};
    const xgl_work_budget_t budget{128U, 10U};
    ASSERT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);
    uint32_t delay = 0U;
    ASSERT_TRUE(xgl_deadline_query(handle, 0U, nullptr, &delay));
    EXPECT_EQ(delay, 10U);
    EXPECT_EQ(xgl_step(handle, 9U, &budget), XGL_OK);
    EXPECT_EQ(xgl_step(handle, 10U, &budget), XGL_ERR_TIMEOUT);
    EXPECT_EQ(timeouts, 1U);
    EXPECT_EQ(reads, 1U);
    ASSERT_TRUE(xgl_deadline_query(handle, 10U, nullptr, &delay));
    EXPECT_EQ(delay, 990U);
    EXPECT_EQ(xgl_step(handle, 11U, &budget), XGL_OK);
    EXPECT_EQ(timeouts, 1U);
}

TEST_F(XglRuntimeContractTest, ZeroRxBudgetStillExpiresPartialFrames) {
    input = {XGL_WIRE_MAGIC_0};
    xgl_work_budget_t budget{128U, 10U};
    ASSERT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);
    budget.rx_bytes = 0U;
    EXPECT_EQ(xgl_step(handle, 10U, &budget), XGL_ERR_TIMEOUT);
    EXPECT_EQ(timeouts, 1U);
    EXPECT_EQ(reads, 1U);
}

TEST_F(XglRuntimeContractTest,
       LatestTimeoutAppliesWithoutRestartingPartialFrame) {
    input = {XGL_WIRE_MAGIC_0};
    xgl_work_budget_t budget{128U, 100U};
    ASSERT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);
    budget.receive_timeout_ms = 20U;
    budget.rx_bytes = 0U;
    ASSERT_EQ(xgl_step(handle, 5U, &budget), XGL_OK);
    uint32_t delay = 0U;
    ASSERT_TRUE(xgl_deadline_query(handle, 5U, nullptr, &delay));
    EXPECT_EQ(delay, 15U);
    EXPECT_EQ(xgl_step(handle, 20U, &budget), XGL_ERR_TIMEOUT);
}

TEST_F(XglRuntimeContractTest, DisabledTimeoutRetainsPartialFrameAcrossPolls) {
    AppendFrame();
    xgl_work_budget_t budget{1U, 0U};
    ASSERT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);
    budget.rx_bytes = 128U;
    EXPECT_EQ(xgl_step(handle, 1000U, &budget), XGL_OK);
    EXPECT_EQ(timeouts, 0U);
    EXPECT_EQ(deliveries, 1U);
}

TEST_F(XglRuntimeContractTest, PartialFrameDeadlineSurvivesClockWrap) {
    input = {XGL_WIRE_MAGIC_0};
    xgl_work_budget_t budget{128U, 10U};
    constexpr uint32_t started = UINT32_MAX - 4U;
    ASSERT_EQ(xgl_step(handle, started, &budget), XGL_OK);
    uint32_t delay = 0U;
    ASSERT_TRUE(xgl_deadline_query(handle, 3U, nullptr, &delay));
    EXPECT_EQ(delay, 2U);
    budget.rx_bytes = 0U;
    EXPECT_EQ(xgl_step(handle, 5U, &budget), XGL_ERR_TIMEOUT);
    EXPECT_EQ(timeouts, 1U);
}

TEST_F(XglRuntimeContractTest,
       BatchReturnsFirstDeliveryErrorAndConsumesLaterFrames) {
    AppendFrame();
    AppendFrame();
    AppendFrame();
    results = {XGL_ERR_BUSY, XGL_ERR_NO_MEMORY, XGL_OK};
    const xgl_work_budget_t budget{128U, 100U};
    EXPECT_EQ(xgl_step(handle, 0U, &budget), XGL_ERR_BUSY);
    EXPECT_EQ(deliveries, 3U);
    EXPECT_EQ(cursor, input.size());
    EXPECT_EQ(xgl_step(handle, 1000U, &budget), XGL_OK);
    EXPECT_EQ(deliveries, 3U);
}

TEST_F(XglRuntimeContractTest, ReceiveFailureDoesNotSkipReliableMaintenance) {
    const uint8_t payload = 7U;
    xgl_tx_data_t request{};
    request.target_id = 2U;
    request.data = &payload;
    request.data_len = 1U;
    request.reliable = true;
    request.timeout_ms = 10U;
    ASSERT_EQ(xgl_send_at(handle, &request, 0U), XGL_OK);
    const size_t initial_writes = writes;
    AppendFrame();
    results = {XGL_ERR_NO_MEMORY};
    const xgl_work_budget_t budget{128U, 100U};
    EXPECT_EQ(xgl_step(handle, 10U, &budget), XGL_ERR_NO_MEMORY);
    EXPECT_GT(writes, initial_writes);
}

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
        config = xgl_test_config();
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
    xgl_handle_t handle = xgl_test_init_allocated(&config);

    ASSERT_NE(handle, nullptr);

    xgl_destroy(handle);
}

/**
 * \brief           Test instance creation with NULL configuration
 */
TEST_F(XglInstanceTest, CreateWithNullConfig) {
    xgl_handle_t handle = xgl_test_init_allocated(NULL);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Reject creation without reliable packet capacity
 */
TEST_F(XglInstanceTest, CreateWithZeroTxPacketCapacity) {
    config.features.max_tx_packets = 0U;

    xgl_handle_t handle = xgl_test_init_allocated(&config);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid RX buffer size
 */
TEST_F(XglInstanceTest, CreateWithInvalidRxBufferSize) {
    config.memory.rx_buffer_size = 0;

    xgl_handle_t handle = xgl_test_init_allocated(&config);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid ACK timeout
 */
TEST_F(XglInstanceTest, CreateWithInvalidAckTimeout) {
    config.protocol.ack_timeout_ms = 0;

    xgl_handle_t handle = xgl_test_init_allocated(&config);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with invalid max frame size
 */
TEST_F(XglInstanceTest, CreateWithInvalidMaxFrameSize) {
    config.protocol.max_frame_size = 0;

    xgl_handle_t handle = xgl_test_init_allocated(&config);

    EXPECT_EQ(handle, nullptr);
}

/**
 * \brief           Test instance creation with frame size too small
 */
TEST_F(XglInstanceTest, CreateWithFrameSizeTooSmall) {
    config.protocol.max_frame_size = 10; /* Less than header + CRC */

    xgl_handle_t handle = xgl_test_init_allocated(&config);

    EXPECT_EQ(handle, nullptr);
}

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance initialization
 */
TEST_F(XglInstanceTest, InitializeInstance) {
    xgl_handle_t handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);

    xgl_destroy(handle);
}

/**
 * \brief           Test initialization with a NULL output pointer
 */
TEST_F(XglInstanceTest, InitializeNullHandle) {
    xgl_error_t err =
        xgl_init_allocated(&config, xgl_test_host_allocator(), nullptr);

    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Destruction Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test instance destruction
 */
TEST_F(XglInstanceTest, DestroyInstance) {
    xgl_handle_t handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);

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

TEST_F(XglInstanceTest, InitializesApplicationCapacityBudgets) {
    for (auto workload : {xgl_test_two_packet_config(),
                          xgl_test_config(256, 288, 4, 2, 4, 4, 5),
                          xgl_test_config(512, 544, 8, 8, 16, 16, 5),
                          xgl_test_config(1024, 1056, 16, 16, 64, 64, 7)}) {
        xgl_handle_t instance = xgl_test_init_allocated(&workload);
        ASSERT_NE(instance, nullptr);
        xgl_destroy(instance);
    }
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

    xgl_handle_t handle1 = xgl_test_init_allocated(&config1);
    xgl_handle_t handle2 = xgl_test_init_allocated(&config2);

    ASSERT_NE(handle1, nullptr);
    ASSERT_NE(handle2, nullptr);
    EXPECT_NE(handle1, handle2);

    const xgl_work_budget_t budget = {};
    EXPECT_EQ(xgl_step(handle1, 0U, &budget), XGL_OK);
    EXPECT_EQ(xgl_step(handle2, 0U, &budget), XGL_OK);

    xgl_destroy(handle1);
    xgl_destroy(handle2);
}

TEST_F(XglInstanceTest, NextTimeoutReportsNoWorkWhenIdleWithoutRoutes) {
    xgl_handle_t handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);

    uint32_t delay = 17U;
    EXPECT_FALSE(xgl_deadline_query(handle, 0U, nullptr, &delay));

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
                                  /* read_freq_hz */ 1}};

    config.route_table = routes;
    config.route_table_len = 1;
    config.protocol.ack_timeout_ms = 500;

    xgl_handle_t handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);

    uint32_t delay = 17U;
    ASSERT_TRUE(xgl_deadline_query(handle, 0U, nullptr, &delay));
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

    ASSERT_TRUE(xgl_deadline_query(handle, 0U, nullptr, &delay));
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
                                  /* read_freq_hz */ 100},
                                 {/* target_id */ 3,
                                  /* phy */ &phy,
                                  /* max_frame_size */ 256,
                                  /* read_freq_hz */ 100}};

    config.route_table = routes;
    config.route_table_len = 2;

    xgl_handle_t handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);

    xgl_destroy(handle);
}

/**
 * \brief           Test instance with invalid route table
 */
TEST_F(XglInstanceTest, InstanceWithInvalidRouteTable) {
    config.route_table = NULL;
    config.route_table_len = 5; /* Non-zero length but NULL pointer */

    xgl_handle_t handle = xgl_test_init_allocated(&config);

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
    xgl_route_item_t routes[] = {{2, &phy, 128, 100}, {3, &phy, 128, 100}};
    config.route_table = routes;
    config.route_table_len = 2;
    xgl_handle_t handle = xgl_test_init_allocated(&config);
    ASSERT_NE(handle, nullptr);
    xgl_work_budget_t budget{64, 1000};
    ASSERT_EQ(xgl_step(handle, 0, &budget), XGL_OK);
    EXPECT_EQ(reads, 1U);
    uint32_t delay = 0;
    ASSERT_TRUE(xgl_deadline_query(handle, 0, nullptr, &delay));
    EXPECT_EQ(delay, 10U);
    EXPECT_EQ(xgl_step(handle, 9, &budget), XGL_OK);
    EXPECT_EQ(reads, 1U);
    EXPECT_EQ(xgl_step(handle, 10, &budget), XGL_OK);
    EXPECT_EQ(reads, 2U);
    xgl_destroy(handle);
}

/**
 * \brief           Reject impossible route counts before reading route storage
 */
TEST_F(XglInstanceTest, RejectsOversizedRouteCount) {
    xgl_route_item_t route{};
    config.route_table = &route;
    config.route_table_len = static_cast<size_t>(UINT16_MAX) + 1U;
    EXPECT_EQ(xgl_test_measure_config(&config), XGL_ERR_INVALID_PARAM);
}

/**
 * \brief           Reserve bounded receive buffering in static storage
 */
TEST_F(XglInstanceTest, StaticWorkspaceSupportsReceiveWindow) {
    config = xgl_test_single_packet_config();
    xgl_memory_requirements_t requirements{};
    ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);
    const size_t single_slot = requirements.size;
    config.protocol.window_size = 2U;
    config.features.max_rx_buffered_packets = 1U;
    ASSERT_EQ(xgl_memory_requirements(&config, &requirements), XGL_OK);
    EXPECT_GT(requirements.size, single_slot);
    std::vector<xgl_max_align_t> storage(
        (requirements.size + sizeof(xgl_max_align_t) - 1U) /
        sizeof(xgl_max_align_t));
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
        size_t live = 0U;
        bool fail = true;
    } state;

    xgl_allocator_t service = {
        &state,
        [](void* ctx, size_t size) -> void* {
            auto* allocator = static_cast<FailureAllocator*>(ctx);
            ++allocator->calls;
            if (allocator->fail) {
                return nullptr;
            }
            void* result = std::malloc(size);
            if (result != nullptr) {
                ++allocator->live;
            }
            return result;
        },
        [](void* ctx, void* ptr) {
            if (ptr != nullptr) {
                --static_cast<FailureAllocator*>(ctx)->live;
                std::free(ptr);
            }
        }};
    xgl_handle_t handle = nullptr;
    EXPECT_EQ(xgl_init_allocated(&config, &service, &handle),
              XGL_ERR_NO_MEMORY);
    EXPECT_EQ(handle, nullptr);
    EXPECT_EQ(state.calls, 1U);
    EXPECT_EQ(state.live, 0U);

    state = {0U, 0U, false};
    ASSERT_EQ(xgl_init_allocated(&config, &service, &handle), XGL_OK);
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->config, &config);
    EXPECT_EQ(state.calls, 1U);
    EXPECT_EQ(state.live, 1U);
    xgl_destroy(handle);
    EXPECT_EQ(state.live, 0U);
}
