/**
 * \file            test_api_contracts.cpp
 * \brief           Explicit creation errors and budget-aware scheduling
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-02
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_config.h"
#include "test_host_allocator.h"

#include <gtest/gtest.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl.h>
#include <xgl/xgl_alloc.h>
#include <xgl/xgl_runtime.h>

class XglApiContractsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        config = xgl_test_single_packet_config();
        config.source_id = 1U;
        config.route_table = &route;
        config.route_table_len = 1U;
        config.callback_user_data = this;
        phy = {Transmit, Receive, this};
        route = {2U, &phy, 128U, 1U};
    }

    void TearDown() override {
        xgl_destroy(handle);
    }

    void Initialize() {
        ASSERT_EQ(
            xgl_init_allocated(&config, xgl_test_host_allocator(), &handle),
            XGL_OK);
        ASSERT_NE(handle, nullptr);
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

TEST_F(XglApiContractsTest, CreationReportsInvalidConfiguration) {
    xgl_handle_t output = reinterpret_cast<xgl_handle_t>(&config);
    EXPECT_EQ(xgl_init_allocated(nullptr, xgl_test_host_allocator(), &output),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(output, nullptr);
    config.source_id = 0U;
    EXPECT_EQ(xgl_init_allocated(&config, xgl_test_host_allocator(), &output),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(output, nullptr);
    EXPECT_EQ(xgl_init_allocated(&config, xgl_test_host_allocator(), nullptr),
              XGL_ERR_NULL_POINTER);
}

TEST_F(XglApiContractsTest, AllocatedInitReportsBackendFailureAndReturnsReady) {
    struct FailingBackend {
        size_t calls = 0U;
        size_t frees = 0U;
    } backend;

    const xgl_allocator_t allocator = {
        &backend,
        [](void* user, size_t) -> void* {
            ++static_cast<FailingBackend*>(user)->calls;
            return nullptr;
        },
        [](void* user, void*) { ++static_cast<FailingBackend*>(user)->frees; }};
    EXPECT_EQ(xgl_init_allocated(&config, &allocator, &handle),
              XGL_ERR_NO_MEMORY);
    EXPECT_EQ(handle, nullptr);
    EXPECT_EQ(backend.calls, 1U);
    EXPECT_EQ(backend.frees, 0U);

    const xgl_allocator_t invalid = {};
    EXPECT_EQ(xgl_init_allocated(&config, &invalid, &handle),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(handle, nullptr);

    Initialize();
    const xgl_work_budget_t budget = {0U, 10U};
    EXPECT_EQ(xgl_step(handle, 0U, &budget), XGL_OK);
    EXPECT_EQ(reads, 0U);
    EXPECT_EQ(writes, 0U);
}

TEST_F(XglApiContractsTest, AllocatedInitReleasesMisalignedWorkspaceOnce) {
    struct Backend {
        alignas(xgl_max_align_t) uint8_t storage[8192] = {};
        size_t allocations = 0U;
        size_t releases = 0U;
    } backend;

    const xgl_allocator_t allocator = {
        &backend,
        [](void* context, size_t size) -> void* {
            auto& state = *static_cast<Backend*>(context);
            ++state.allocations;
            EXPECT_LT(size, sizeof(state.storage));
            return size < sizeof(state.storage) ? state.storage + 1U : nullptr;
        },
        [](void* context, void* storage) {
            auto& state = *static_cast<Backend*>(context);
            EXPECT_EQ(storage, state.storage + 1U);
            ++state.releases;
        }};

    handle = reinterpret_cast<xgl_handle_t>(&config);
    EXPECT_EQ(xgl_init_allocated(&config, &allocator, &handle),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(handle, nullptr);
    EXPECT_EQ(backend.allocations, 1U);
    EXPECT_EQ(backend.releases, 1U);
}

TEST_F(XglApiContractsTest, ZeroRxQueryExcludesPollingAndDoesNotChangeLegacy) {
    Initialize();
    const xgl_work_budget_t budget = {0U, 10U};
    uint32_t delay = 123U;
    EXPECT_FALSE(xgl_deadline_query(handle, 0U, &budget, &delay));
    EXPECT_EQ(delay, 123U);
    EXPECT_EQ(reads, 0U);
    ASSERT_TRUE(xgl_deadline_query(handle, 0U, nullptr, &delay));
    EXPECT_EQ(delay, 0U);
}

TEST_F(XglApiContractsTest, ProposedParserPolicyIsPureAndPreservesTimestamp) {
    Initialize();
    partial = true;
    const xgl_work_budget_t active = {1U, 100U};
    ASSERT_EQ(xgl_step(handle, 0U, &active), XGL_OK);
    const xgl_work_budget_t proposed = {0U, 20U};
    uint32_t delay = 0U;
    ASSERT_TRUE(xgl_deadline_query(handle, 5U, &proposed, &delay));
    EXPECT_EQ(delay, 15U);
    ASSERT_TRUE(xgl_deadline_query(handle, 5U, nullptr, &delay));
    EXPECT_EQ(delay, 95U);
    EXPECT_EQ(reads, 1U);
    const xgl_work_budget_t disabled = {0U, 0U};
    delay = 123U;
    EXPECT_FALSE(xgl_deadline_query(handle, 5U, &disabled, &delay));
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
    ASSERT_TRUE(xgl_deadline_query(handle, 3U, &budget, &delay));
    EXPECT_EQ(delay, 2U);
    ASSERT_TRUE(xgl_deadline_query(handle, 5U, &budget, &delay));
    EXPECT_EQ(delay, 0U);
    EXPECT_EQ(writes, submitted_writes);
    EXPECT_EQ(reads, 0U);
}

TEST_F(XglApiContractsTest, ProposedParserDeadlineSurvivesClockWrap) {
    Initialize();
    partial = true;
    const xgl_work_budget_t active = {1U, 100U};
    ASSERT_EQ(xgl_step(handle, UINT32_MAX - 4U, &active), XGL_OK);
    const xgl_work_budget_t proposed = {0U, 10U};
    uint32_t delay = 0U;
    ASSERT_TRUE(xgl_deadline_query(handle, 3U, &proposed, &delay));
    EXPECT_EQ(delay, 2U);
    ASSERT_TRUE(xgl_deadline_query(handle, 5U, &proposed, &delay));
    EXPECT_EQ(delay, 0U);
    ASSERT_TRUE(xgl_deadline_query(handle, 5U, nullptr, &delay));
    EXPECT_EQ(delay, 90U);
    EXPECT_EQ(xgl_step(handle, 5U, &proposed), XGL_ERR_TIMEOUT);
    EXPECT_EQ(reads, 1U);
}

TEST_F(XglApiContractsTest,
       BudgetQueryRejectsInvalidInputsWithoutOutputWrites) {
    const xgl_work_budget_t budget = {1U, 10U};
    uint32_t delay = 123U;
    EXPECT_FALSE(xgl_deadline_query(nullptr, 0U, &budget, &delay));
    Initialize();
    const xgl_work_budget_t invalid = {1U, UINT32_C(0x80000000)};
    EXPECT_FALSE(xgl_deadline_query(handle, 0U, &invalid, &delay));
    EXPECT_FALSE(xgl_deadline_query(handle, 0U, &budget, nullptr));
    EXPECT_EQ(delay, 123U);
    ASSERT_TRUE(xgl_deadline_query(handle, 0U, &budget, &delay));
    EXPECT_EQ(delay, 0U);
}
