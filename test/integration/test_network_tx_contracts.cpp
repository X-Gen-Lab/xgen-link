/**
 * \file            test_network_tx_contracts.cpp
 * \brief           Public send admission and cross-layer TX statistics
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <xgl/xgl.h>

#include "test_host_allocator.h"
#include <array>
#include <gtest/gtest.h>

namespace {

enum class SendApi { Copied, LegacyZeroCopy, ScopedZeroCopy };

class XglNetworkTxContractTest : public ::testing::Test {
  protected:
    void SetUp() override {
        phy.tx = [](const uint8_t*, size_t, void* user) {
            auto* fixture = static_cast<XglNetworkTxContractTest*>(user);
            ++fixture->driver_calls;
            return fixture->driver_result;
        };
        phy.rx = [](uint8_t*, size_t* length, void*) {
            *length = 0U;
            return XGL_OK;
        };
        phy.user_data = this;
        xgl_config_get_preset_boot(&config);
        config.memory.allocator = xgl_test_host_allocator();
        route.target_id = 2U;
        route.phy = &phy;
        route.max_frame_size = config.protocol.max_frame_size;
        route.read_freq_hz = 100U;
        config.route_table = &route;
        config.route_table_len = 1U;
        config.error_callback = [](xgl_handle_t, xgl_error_t, const char*,
                                   void* user) {
            ++static_cast<XglNetworkTxContractTest*>(user)->error_callbacks;
        };
        config.callback_user_data = this;
        handle = xgl_create(&config);
        ASSERT_NE(handle, nullptr);
        ASSERT_EQ(xgl_init(handle), XGL_OK);
        buffer[XGL_FRAME_HEADER_SIZE] = 0x17U;
        buffer[XGL_FRAME_HEADER_SIZE + 1U] = 0x28U;
        buffer[XGL_FRAME_HEADER_SIZE + 2U] = 0x39U;
    }

    void TearDown() override {
        xgl_destroy(handle);
    }

    void ResetCounters() {
        ASSERT_EQ(xgl_stats_reset(handle), XGL_OK);
        driver_calls = 0U;
        error_callbacks = 0U;
    }

    xgl_error_t Send(SendApi api, size_t length = 3U, uint8_t priority = 0U) {
        xgl_tx_data_t copied = {};
        copied.target_id = 2U;
        copied.data = buffer.data() + XGL_FRAME_HEADER_SIZE;
        copied.data_len = length;
        copied.priority = priority;
        xgl_tx_zerocopy_request_t request = {};
        request.data.buffer = buffer.data();
        request.data.buffer_size = buffer.size();
        request.data.data_offset = XGL_FRAME_HEADER_SIZE;
        request.data.data_len = length;
        request.data.target_id = copied.target_id;
        request.data.priority = priority;
        switch (api) {
            case SendApi::Copied:
                return xgl_send_at(handle, &copied, 0U);
            case SendApi::LegacyZeroCopy:
                return xgl_send_zerocopy_at(handle, &request.data, 0U);
            case SendApi::ScopedZeroCopy:
                return xgl_send_zerocopy_scoped_at(handle, &request, 0U);
        }
        return XGL_ERR_INVALID_PARAM;
    }

    void ExpectStatistics(uint64_t packets, uint64_t errors) {
        xgl_statistics_t statistics = {};
        ASSERT_EQ(xgl_stats_get(handle, &statistics), XGL_OK);
        EXPECT_EQ(statistics.transport.tx_packets, packets);
        EXPECT_EQ(statistics.network.tx_packets, packets);
        EXPECT_EQ(statistics.datalink.tx_packets, packets);
        EXPECT_EQ(statistics.transport.tx_bytes, packets * 3U);
        EXPECT_EQ(statistics.network.tx_bytes, packets * 3U);
        EXPECT_EQ(statistics.datalink.tx_bytes,
                  packets * (XGL_FRAME_HEADER_SIZE + 3U + XGL_CRC16_SIZE));
        EXPECT_EQ(statistics.transport.tx_errors, errors);
        EXPECT_EQ(statistics.network.tx_errors, errors);
        EXPECT_EQ(statistics.datalink.tx_errors, errors);
    }

    xgl_config_t config = {};
    xgl_route_item_t route = {};
    xgl_phy_ops_t phy = {};
    xgl_handle_t handle = nullptr;
    std::array<uint8_t, 128> buffer = {};
    xgl_error_t driver_result = XGL_OK;
    size_t driver_calls = 0U;
    size_t error_callbacks = 0U;
};

TEST_F(XglNetworkTxContractTest, DriverFailuresCountOnceAcrossAllSendApis) {
    for (const SendApi api :
         {SendApi::Copied, SendApi::LegacyZeroCopy, SendApi::ScopedZeroCopy}) {
        for (const xgl_error_t failure :
             {XGL_ERR_BUSY, XGL_ERR_TX_FAILED, XGL_ERR_INVALID_PARAM}) {
            SCOPED_TRACE(::testing::Message() << "api=" << static_cast<int>(api)
                                              << " driver_result=" << failure);
            ResetCounters();
            driver_result = failure;
            EXPECT_EQ(Send(api), failure);
            EXPECT_EQ(driver_calls, 1U);
            EXPECT_EQ(error_callbacks, 1U);
            ExpectStatistics(0U, 1U);
        }
    }
}

TEST_F(XglNetworkTxContractTest,
       SuccessfulRetryAfterDriverBusyCountsOnlyAcceptedBytes) {
    for (const SendApi api :
         {SendApi::Copied, SendApi::LegacyZeroCopy, SendApi::ScopedZeroCopy}) {
        SCOPED_TRACE(static_cast<int>(api));
        ResetCounters();
        driver_result = XGL_ERR_BUSY;
        EXPECT_EQ(Send(api), XGL_ERR_BUSY);
        driver_result = XGL_OK;
        EXPECT_EQ(Send(api), XGL_OK);
        EXPECT_EQ(driver_calls, 2U);
        EXPECT_EQ(error_callbacks, 1U);
        ExpectStatistics(1U, 1U);
    }
}

TEST_F(XglNetworkTxContractTest,
       InvalidPublicArgumentsDoNotCountAsTransmissionFailures) {
    for (const SendApi api :
         {SendApi::Copied, SendApi::LegacyZeroCopy, SendApi::ScopedZeroCopy}) {
        SCOPED_TRACE(static_cast<int>(api));
        EXPECT_EQ(Send(api, 0U), XGL_ERR_INVALID_PARAM);
        EXPECT_EQ(Send(api, 3U, 8U), XGL_ERR_INVALID_PARAM);
    }
    EXPECT_EQ(xgl_send_at(handle, nullptr, 0U), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_send_zerocopy_at(handle, nullptr, 0U), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_send_zerocopy_scoped_at(handle, nullptr, 0U),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(driver_calls, 0U);
    EXPECT_EQ(error_callbacks, 0U);
    ExpectStatistics(0U, 0U);
}

TEST_F(XglNetworkTxContractTest, LayoutQueriesDoNotCountOrCallDriver) {
    xgl_tx_zerocopy_request_t request = {};
    request.data.target_id = 2U;
    request.data.data_len = 3U;
    xgl_tx_frame_layout_t layout = {};
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout), XGL_OK);
    request.data.target_id = 3U;
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout),
              XGL_ERR_ROUTE_NOT_FOUND);
    EXPECT_EQ(layout.payload_offset, 0U);
    EXPECT_EQ(layout.frame_size, 0U);
    request.data.target_id = 2U;
    request.data.data_len = 0U;
    EXPECT_EQ(xgl_send_zerocopy_layout(handle, &request, &layout),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(driver_calls, 0U);
    EXPECT_EQ(error_callbacks, 0U);
    ExpectStatistics(0U, 0U);
}

} /* namespace */
