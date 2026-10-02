/**
 * \file            test_extended_options.cpp
 * \brief           Versioned optional runtime and independent message storage
 */

#include "test_host_allocator.h"
#include <gtest/gtest.h>
#include <xgl/xgl.h>

TEST(XglExtendedOptions, RejectsForeignOptionsBeforeAllocation) {
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    xgl_test_use_host_allocator(&config);
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    options.version++;
    xgl_handle_t handle = nullptr;
    EXPECT_EQ(xgl_create_with_options(&config, &options, &handle),
              XGL_ERR_INVALID_VERSION);
    EXPECT_EQ(handle, nullptr);
}

#if XGL_FEATURE_ADVANCED_RUNTIME
TEST(XglExtendedOptions, DefaultOptionsPreserveMeasuredStorage) {
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    xgl_memory_report_t legacy = {};
    xgl_memory_report_t extended = {};
    ASSERT_EQ(xgl_memory_report(&config, &legacy), XGL_OK);
    ASSERT_EQ(xgl_memory_report_with_options(&config, &options, &extended),
              XGL_OK);
    EXPECT_EQ(legacy.requirements.size, extended.requirements.size);
    EXPECT_EQ(legacy.fragment_bytes, extended.fragment_bytes);
}

#if XGL_FEATURE_FRAGMENTATION
TEST(XglExtendedOptions, MessageSlotsAreIndependentFromPeerCapacity) {
    xgl_config_t config = XGL_CONFIG_PRESET_SMALL;
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    options.tx_message_slots = 1U;
    options.rx_message_slots = 2U;
    xgl_memory_report_t legacy = {};
    xgl_memory_report_t extended = {};
    ASSERT_EQ(xgl_memory_report(&config, &legacy), XGL_OK);
    ASSERT_EQ(xgl_memory_report_with_options(&config, &options, &extended),
              XGL_OK);
    EXPECT_EQ(legacy.requirements.size - extended.requirements.size, 5120U);
    EXPECT_EQ(legacy.peer_bytes, extended.peer_bytes);
    EXPECT_EQ(legacy.fragment_bytes - extended.fragment_bytes, 5120U);
}
#endif

TEST(XglExtendedOptions, InvalidLifetimeDoesNotModifyReport) {
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    options.tx_lifetime_ms = UINT32_C(0x80000000);
    xgl_memory_report_t report = {};
    report.requirements.size = 123U;
    EXPECT_EQ(xgl_memory_report_with_options(&config, &options, &report),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(report.requirements.size, 123U);
}
#else
TEST(XglExtendedOptions, DisabledCapabilityIsExplicit) {
    xgl_config_t config;
    xgl_config_get_preset_boot(&config);
    xgl_instance_options_t options = XGL_INSTANCE_OPTIONS_INIT;
    xgl_memory_report_t report = {};
    EXPECT_EQ(xgl_memory_report_with_options(&config, &options, &report),
              XGL_ERR_UNSUPPORTED);
}
#endif
