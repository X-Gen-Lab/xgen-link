/**
 * \file            test_api_contracts.cpp
 * \brief           Explicit creation errors and budget-aware scheduling
 */

#include "test_host_allocator.h"

#include <gtest/gtest.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl.h>

class XglApiContractsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        xgl_config_get_preset_boot(&config);
        config.source_id = 1U;
        config.route_table = &route;
        config.route_table_len = 1U;
        config.callback_user_data = this;
        phy = {Transmit, Receive, this};
        route = {2U, &phy, 128U, 1U, 1U};
        xgl_test_use_host_allocator(&config);
    }

    void TearDown() override {
        xgl_destroy(handle);
    }

    void Initialize() {
        handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr);
        ASSERT_EQ(xgl_init(handle), XGL_OK);
    }

    static xgl_error_t Transmit(const uint8_t*, size_t, void* user) {
        ++static_cast<XglApiContractsTest*>(user)->writes;
        return XGL_OK;
    }

    static xgl_error_t Receive(uint8_t* bytes, size_t* length, void* user) {
        auto& self = *static_cast<XglApiContractsTest*>(user);
        ++self.reads;
        if (self.partial && *length != 0U) {
            bytes[0] = XGL_WIRE_MAGIC_0;
            *length = 1U;
            self.partial = false;
        } else {
            *length = 0U;
        }
        return XGL_OK;
    }

    xgl_config_t config = {};
    xgl_phy_ops_t phy = {};
    xgl_route_item_t route = {};
    xgl_handle_t handle = nullptr;
    size_t reads = 0U;
    size_t writes = 0U;
    bool partial = false;
};

TEST_F(XglApiContractsTest, CreationReturnsConfigurationAndAbiErrors) {
    xgl_handle_t output = reinterpret_cast<xgl_handle_t>(&config);
    EXPECT_EQ(xgl_create_with_error(nullptr, &output), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(output, nullptr);
    config.source_id = 0U;
    EXPECT_EQ(xgl_create_with_error(&config, &output), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(output, nullptr);
    EXPECT_EQ(xgl_create_with_error_checked(&config, sizeof(config) - 1U,
                                            XGL_CONFIG_ABI_VERSION,
                                            XGL_BUILD_CONFIG_ID, &output),
              XGL_ERR_INVALID_VERSION);
    EXPECT_EQ(output, nullptr);
    EXPECT_EQ(xgl_create_with_error_checked(&config, sizeof(config),
                                            XGL_CONFIG_ABI_VERSION + 1U,
                                            XGL_BUILD_CONFIG_ID, &output),
              XGL_ERR_INVALID_VERSION);
    EXPECT_EQ(output, nullptr);
    EXPECT_EQ(xgl_create_with_error_checked(&config, sizeof(config),
                                            XGL_CONFIG_ABI_VERSION,
                                            XGL_BUILD_CONFIG_ID + 1U, &output),
              XGL_ERR_INVALID_VERSION);
    EXPECT_EQ(output, nullptr);
    EXPECT_EQ(xgl_create_with_error(&config, nullptr), XGL_ERR_NULL_POINTER);
}

TEST_F(XglApiContractsTest, CreationReportsBackendFailureAndReservesOnly) {
    struct FailingBackend {
        size_t calls = 0U;
        size_t frees = 0U;
    } backend;

    const xgm_allocator_t allocator = {
        &backend,
        [](void* user, size_t) -> void* {
            ++static_cast<FailingBackend*>(user)->calls;
            return nullptr;
        },
        [](void* user, void*) { ++static_cast<FailingBackend*>(user)->frees; }};
    config.memory.allocator = &allocator;
    EXPECT_EQ(xgl_create_with_error(&config, &handle), XGL_ERR_NO_MEMORY);
    EXPECT_EQ(handle, nullptr);
    EXPECT_EQ(backend.calls, 1U);
    EXPECT_EQ(backend.frees, 0U);

    const xgm_allocator_t invalid = {};
    config.memory.allocator = &invalid;
    EXPECT_EQ(xgl_create_with_error(&config, &handle), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(handle, nullptr);

    config.memory.allocator = xgm_allocator_libc();
    ASSERT_EQ(xgl_create_with_error(&config, &handle), XGL_OK);
    ASSERT_NE(handle, nullptr);
    const xgl_work_budget_t budget = {0U, 10U};
    EXPECT_EQ(xgl_step(handle, 0U, &budget), XGL_ERR_NOT_INITIALIZED);
    EXPECT_EQ(reads, 0U);
    EXPECT_EQ(writes, 0U);
    EXPECT_EQ(xgl_init(handle), XGL_OK);
}

TEST_F(XglApiContractsTest, ZeroRxQueryExcludesPollingAndDoesNotChangeLegacy) {
    Initialize();
    const xgl_work_budget_t budget = {0U, 10U};
    uint32_t delay = 123U;
    EXPECT_FALSE(xgl_next_timeout_for_work(handle, 0U, &budget, &delay));
    EXPECT_EQ(delay, 123U);
    EXPECT_EQ(reads, 0U);
    ASSERT_TRUE(xgl_next_timeout(handle, 0U, &delay));
    EXPECT_EQ(delay, 0U);
}

TEST_F(XglApiContractsTest, ProposedParserPolicyIsPureAndPreservesTimestamp) {
    Initialize();
    partial = true;
    const xgl_work_budget_t active = {1U, 100U};
    ASSERT_EQ(xgl_step(handle, 0U, &active), XGL_OK);
    const xgl_work_budget_t proposed = {0U, 20U};
    uint32_t delay = 0U;
    ASSERT_TRUE(xgl_next_timeout_for_work(handle, 5U, &proposed, &delay));
    EXPECT_EQ(delay, 15U);
    ASSERT_TRUE(xgl_next_timeout(handle, 5U, &delay));
    EXPECT_EQ(delay, 95U);
    EXPECT_EQ(reads, 1U);
    const xgl_work_budget_t disabled = {0U, 0U};
    delay = 123U;
    EXPECT_FALSE(xgl_next_timeout_for_work(handle, 5U, &disabled, &delay));
    EXPECT_EQ(delay, 123U);
    EXPECT_EQ(xgl_step(handle, 20U, &proposed), XGL_ERR_TIMEOUT);
}

TEST_F(XglApiContractsTest, ZeroRxQueryKeepsReliableMaintenanceAcrossWrap) {
    Initialize();
    const uint8_t payload = 7U;
    xgl_tx_data_t request = {};
    request.target_id = 2U;
    request.data = &payload;
    request.data_len = 1U;
    request.reliable = true;
    request.timeout_ms = 10U;
    ASSERT_EQ(xgl_send_at(handle, &request, UINT32_MAX - 4U), XGL_OK);
    const size_t submitted_writes = writes;
    ASSERT_GT(submitted_writes, 0U);
    const xgl_work_budget_t budget = {0U, 0U};
    uint32_t delay = 0U;
    ASSERT_TRUE(xgl_next_timeout_for_work(handle, 3U, &budget, &delay));
    EXPECT_EQ(delay, 2U);
    ASSERT_TRUE(xgl_next_timeout_for_work(handle, 5U, &budget, &delay));
    EXPECT_EQ(delay, 0U);
    EXPECT_EQ(writes, submitted_writes);
    EXPECT_EQ(reads, 0U);
}

TEST_F(XglApiContractsTest,
       BudgetQueryRejectsInvalidInputsWithoutOutputWrites) {
    const xgl_work_budget_t budget = {1U, 10U};
    uint32_t delay = 123U;
    EXPECT_FALSE(xgl_next_timeout_for_work(nullptr, 0U, &budget, &delay));
    handle = xgl_create(&config);
    ASSERT_NE(handle, nullptr);
    EXPECT_FALSE(xgl_next_timeout_for_work(handle, 0U, &budget, &delay));
    ASSERT_EQ(xgl_init(handle), XGL_OK);
    const xgl_work_budget_t invalid = {1U, UINT32_C(0x80000000)};
    EXPECT_FALSE(xgl_next_timeout_for_work(handle, 0U, &invalid, &delay));
    EXPECT_FALSE(xgl_next_timeout_for_work(handle, 0U, nullptr, &delay));
    EXPECT_FALSE(xgl_next_timeout_for_work(handle, 0U, &budget, nullptr));
    EXPECT_EQ(delay, 123U);
    ASSERT_TRUE(xgl_next_timeout_for_work(handle, 0U, &budget, &delay));
    EXPECT_EQ(delay, 0U);
}
