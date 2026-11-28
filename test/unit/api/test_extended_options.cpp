/**
 * \file            test_extended_options.cpp
 * \brief           Versioned optional runtime policy
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-02
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_config.h"
#include "test_host_allocator.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>
#include <wire/xgl_frame.h>
#include <xgl/xgl.h>
#include <xgl/xgl_alloc.h>
#include <xgl/xgl_diagnostics.h>
#include <xgl/xgl_runtime.h>

TEST(XglExtendedOptions, RejectsForeignOptionsBeforeAllocation) {
    xgl_config_t config;
    config = xgl_test_single_packet_config();
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    options.version++;
    config.options = &options;
    xgl_handle_t handle = nullptr;
    EXPECT_EQ(xgl_init_allocated(&config, xgl_test_host_allocator(), &handle),
              XGL_ERR_INVALID_VERSION);
    EXPECT_EQ(handle, nullptr);
}

#if XGL_FEATURE_ADVANCED_RUNTIME
class XglBoundedPollingTest : public ::testing::Test {
  protected:
    xgl_config_t config = {};
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    xgl_phy_ops_t phy = {};
    xgl_route_item_t route = {};
    xgl_handle_t handle = nullptr;
    std::vector<uint8_t> input;
    size_t reads = 0U;
    size_t deliveries = 0U;
    xgl_error_t phy_result = XGL_OK;

    void SetUp() override {
        const uint8_t payload = 0x42U;
        xgl_frame_params_t params = {};
        params.source_id = 2U;
        params.target_id = 1U;
        params.payload = &payload;
        params.payload_len = 1U;
        xgl_frame_t frame = {};
        ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
        input.resize(64U);
        xgl_frame_layout_t layout = {};
        ASSERT_EQ(xgl_frame_encode_into(input.data(), input.size(), &frame, 0U,
                                        &layout),
                  XGL_OK);
        size_t written = 0U;
        ASSERT_EQ(xgl_frame_finalize_crc(input.data(), input.size(), &layout,
                                         &written),
                  XGL_OK);
        input.resize(written);
        phy.user_data = this;
        phy.tx = [](const uint8_t*, size_t, void*) { return XGL_OK; };
        phy.rx = [](uint8_t* bytes, size_t* length, void* user) {
            auto& self = *static_cast<XglBoundedPollingTest*>(user);
            ++self.reads;
            if (self.phy_result != XGL_OK) {
                return self.phy_result;
            }
            *length = std::min(*length, self.input.size());
            if (*length != 0U) {
                std::memcpy(bytes, self.input.data(), *length);
                self.input.erase(self.input.begin(),
                                 self.input.begin() +
                                     static_cast<std::ptrdiff_t>(*length));
            }
            return XGL_OK;
        };
        route = {2U, &phy, 128U, 1U};
        config = xgl_test_single_packet_config();
        config.source_id = 1U;
        config.route_table = &route;
        config.route_table_len = 1U;
        config.rx_user_data = this;
        config.rx_callback = [](xgl_handle_t, const xgl_rx_info_t* info,
                                const uint8_t*, size_t length, void* user) {
            EXPECT_EQ(info->source_id, 2U);
            EXPECT_EQ(length, 1U);
            ++static_cast<XglBoundedPollingTest*>(user)->deliveries;
            return XGL_OK;
        };
        config.options = &options;
        ASSERT_EQ(
            xgl_init_allocated(&config, xgl_test_host_allocator(), &handle),
            XGL_OK);
    }

    void TearDown() override {
        xgl_destroy(handle);
    }
};

TEST_F(XglBoundedPollingTest, PausedParserDoesNotConsumeThePhyPollInterval) {
    const xgl_work_budget_t budget = {128U, 0U};
    xgl_execution_limits_t limits = {0U, 0U};
    xgl_execution_result_t result = {};
    ASSERT_EQ(xgl_step_bounded(handle, 0U, &budget, &limits, &result), XGL_OK);
    ASSERT_TRUE(result.pending);
    ASSERT_EQ(reads, 1U);
    EXPECT_EQ(deliveries, 0U);
    ASSERT_EQ(xgl_step_bounded(handle, 1000U, &budget, &limits, &result),
              XGL_OK);
    EXPECT_EQ(reads, 1U);
    EXPECT_TRUE(result.pending);
    limits.parser_candidates = SIZE_MAX;
    EXPECT_EQ(xgl_step_bounded(handle, 1000U, &budget, &limits, &result),
              XGL_OK);
    EXPECT_EQ(reads, 2U);
    EXPECT_EQ(deliveries, 1U);
    EXPECT_FALSE(result.pending);
    uint32_t delay = 0U;
    ASSERT_TRUE(xgl_deadline_query(handle, 1000U, &budget, &delay));
    EXPECT_EQ(delay, 1000U);
    EXPECT_EQ(xgl_step_bounded(handle, 1999U, &budget, &limits, &result),
              XGL_OK);
    EXPECT_EQ(reads, 2U);
    EXPECT_EQ(xgl_step_bounded(handle, 2000U, &budget, &limits, &result),
              XGL_OK);
    EXPECT_EQ(reads, 3U);
}

TEST_F(XglBoundedPollingTest, DriverFailureStillConsumesThePhyPollInterval) {
    const xgl_work_budget_t budget = {128U, 0U};
    const xgl_execution_limits_t limits = {SIZE_MAX, 0U};
    xgl_execution_result_t result = {};
    input.clear();
    phy_result = XGL_ERR_BUSY;
    EXPECT_EQ(xgl_step_bounded(handle, 0U, &budget, &limits, &result),
              XGL_ERR_BUSY);
    ASSERT_EQ(reads, 1U);
    EXPECT_FALSE(result.pending);
    phy_result = XGL_OK;
    EXPECT_EQ(xgl_step_bounded(handle, 500U, &budget, &limits, &result),
              XGL_OK);
    EXPECT_EQ(reads, 1U);
    EXPECT_EQ(xgl_step_bounded(handle, 1000U, &budget, &limits, &result),
              XGL_OK);
    EXPECT_EQ(reads, 2U);
}

TEST(XglExtendedOptions, DefaultOptionsPreserveMeasuredStorage) {
    xgl_config_t config;
    config = xgl_test_single_packet_config();
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    xgl_memory_report_t legacy = {};
    xgl_memory_report_t extended = {};
    ASSERT_EQ(xgl_memory_report(&config, &legacy), XGL_OK);
    config.options = &options;
    ASSERT_EQ(xgl_memory_report(&config, &extended), XGL_OK);
    EXPECT_EQ(legacy.requirements.size, extended.requirements.size);
    EXPECT_EQ(legacy.requirements.alignment, extended.requirements.alignment);
}

TEST(XglExtendedOptions, InvalidLifetimeDoesNotModifyReport) {
    xgl_config_t config;
    config = xgl_test_single_packet_config();
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    options.tx_lifetime_ms = UINT32_C(0x80000000);
    config.options = &options;
    xgl_memory_report_t report = {};
    report.requirements.size = 123U;
    EXPECT_EQ(xgl_memory_report(&config, &report), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(report.requirements.size, 123U);
}

TEST(XglExtendedOptions, InvalidDescriptorAndCapabilityPreserveOutputs) {
    xgl_config_t config;
    config = xgl_test_single_packet_config();
    const xgl_instance_options_t base = XGL_INSTANCE_OPTIONS_INIT;
    xgl_instance_options_t options = base;
    config.options = &options;
    xgl_memory_report_t report = {};
    report.requirements.size = 123U;
    for (unsigned index = 0U; index < 3U; ++index) {
        options = base;
        if (index == 0U)
            options.size++;
        if (index == 1U)
            options.config_size++;
        if (index == 2U)
            options.version--;
        EXPECT_EQ(xgl_memory_report(&config, &report), XGL_ERR_INVALID_VERSION);
        EXPECT_EQ(report.requirements.size, 123U);
    }
    options = base;
    options.protect_installed_scopes = true;
#if XGL_FEATURE_AUTH
    const xgl_error_t unsupported_auth = XGL_ERR_INVALID_PARAM;
#else
    const xgl_error_t unsupported_auth = XGL_ERR_UNSUPPORTED;
#endif
    EXPECT_EQ(xgl_memory_report(&config, &report), unsupported_auth);
    EXPECT_EQ(report.requirements.size, 123U);
    EXPECT_EQ(xgl_memory_report(nullptr, &report), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_memory_report(&config, nullptr), XGL_ERR_NULL_POINTER);
}

TEST(XglExtendedOptions, StaticAndOwnedInitializationUseTheSameOptions) {
    xgl_config_t config;
    config = xgl_test_single_packet_config();
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    options.tx_lifetime_ms = 200U;
    config.options = &options;
    xgl_memory_report_t report = {};
    ASSERT_EQ(xgl_memory_report(&config, &report), XGL_OK);
    std::vector<xgl_max_align_t> storage(
        (report.requirements.size + sizeof(xgl_max_align_t) - 1U) /
        sizeof(xgl_max_align_t));
    xgl_handle_t handle = nullptr;
    EXPECT_EQ(xgl_init_static(&config, storage.data(),
                              report.requirements.size - 1U, &handle),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(handle, nullptr);
    ASSERT_EQ(xgl_init_static(&config, storage.data(), report.requirements.size,
                              &handle),
              XGL_OK);
    xgl_resource_snapshot_t snapshot = {};
    ASSERT_EQ(xgl_resources_get(handle, &snapshot), XGL_OK);
    EXPECT_EQ(snapshot.peer_count, 0U);
    const xgl_work_budget_t budget = {0U, 0U};
    const xgl_execution_limits_t limits = {0U, 0U};
    xgl_execution_result_t result = {true};
    EXPECT_EQ(xgl_step_bounded(handle, 0U, &budget, &limits, &result), XGL_OK);
    EXPECT_FALSE(result.pending);
    result.pending = true;
    EXPECT_EQ(xgl_step_bounded(handle, 0U, nullptr, &limits, &result),
              XGL_ERR_NULL_POINTER);
    EXPECT_FALSE(result.pending);
    xgl_work_budget_t invalid = budget;
    invalid.receive_timeout_ms = UINT32_C(0x80000000);
    EXPECT_EQ(xgl_step_bounded(handle, 0U, &invalid, &limits, &result),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_resources_get(handle, nullptr), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_resources_get(nullptr, &snapshot), XGL_ERR_NULL_POINTER);
    xgl_destroy(handle);
    handle = nullptr;
    ASSERT_EQ(xgl_init_allocated(&config, xgl_test_host_allocator(), &handle),
              XGL_OK);
    EXPECT_EQ(xgl_resources_get(handle, &snapshot), XGL_OK);
    xgl_destroy(handle);
}
#else
TEST(XglExtendedOptions, DisabledCapabilityIsExplicit) {
    xgl_config_t config;
    config = xgl_test_single_packet_config();
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    config.options = &options;
    xgl_memory_report_t report = {};
    EXPECT_EQ(xgl_memory_report(&config, &report), XGL_ERR_UNSUPPORTED);
}
#endif
