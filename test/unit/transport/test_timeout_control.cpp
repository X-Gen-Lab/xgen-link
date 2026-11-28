/**
 * \file            test_timeout_control.cpp
 * \brief           Public send timeout and retry behavior
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-03-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_config.h"
#include "test_host_allocator.h"
#include <wire/xgl_wire.h>
#include <xgl/xgl.h>
#include <xgl/xgl_inplace.h>

#include <gtest/gtest.h>

class XglTimeoutControlTest : public ::testing::Test {
  protected:
    xgl_handle_t instance = nullptr;
    xgl_phy_ops_t phy = {};
    xgl_route_item_t route = {};
    unsigned data_calls = 0U;
    uint32_t last_number = UINT32_MAX;
    uint8_t payload = 0x42U;

    static xgl_error_t Transmit(const uint8_t* bytes, size_t length,
                                void* context) {
        auto& self = *static_cast<XglTimeoutControlTest*>(context);
        xgl_wire_frame_view_t frame = {};
        const xgl_error_t error =
            xgl_wire_decode_frame(&frame, bytes, length, nullptr);
        EXPECT_EQ(error, XGL_OK);
        if (error == XGL_OK &&
            frame.header.packet_type == XGL_PACKET_TYPE_DATA) {
            ++self.data_calls;
            self.last_number = frame.header.packet_number;
            EXPECT_EQ(frame.payload_len, 1U);
            EXPECT_EQ(frame.payload[0], self.payload);
        }
        return error;
    }

    static xgl_error_t Receive(uint8_t*, size_t* length, void*) {
        *length = 0U;
        return XGL_OK;
    }

    void SetUp() override {
        CreateInstance();
    }

    void TearDown() override {
        xgl_destroy(instance);
    }

    void CreateInstance() {
        xgl_destroy(instance);
        instance = nullptr;
        data_calls = 0U;
        last_number = UINT32_MAX;
        phy = {Transmit, Receive, this};
        route = {2U, &phy, 128U, 1000U};
        xgl_config_t config = {};
        config = xgl_test_single_packet_config();
        config.protocol.ack_timeout_ms = 200U;
        config.features.peer_idle_timeout_ms = 0U;
        config.route_table = &route;
        config.route_table_len = 1U;
        instance = xgl_test_init_allocated(&config);
        ASSERT_NE(instance, nullptr);
    }

    xgl_error_t Send(uint32_t timeout, uint32_t now) {
        xgl_tx_data_t request = {};
        request.target_id = 2U;
        request.data = &payload;
        request.data_len = 1U;
        request.reliable = true;
        request.timeout_ms = timeout;
        return xgl_send_at(instance, &request, now);
    }

    void Step(uint32_t now) {
        const xgl_work_budget_t budget = {};
        ASSERT_EQ(xgl_step(instance, now, &budget), XGL_OK);
    }

    void ExpectFirstRetry(uint32_t start, uint32_t delay) {
        ASSERT_EQ(data_calls, 1U);
        EXPECT_EQ(last_number, 0U);
        Step(start + delay - 1U);
        EXPECT_EQ(data_calls, 1U);
        Step(start + delay);
        EXPECT_EQ(data_calls, 2U);
        EXPECT_EQ(last_number, 0U);
    }
};

TEST_F(XglTimeoutControlTest, TimeoutFieldExists) {
    ASSERT_EQ(Send(5000U, 100U), XGL_OK);
    ExpectFirstRetry(100U, 5000U);
}

TEST_F(XglTimeoutControlTest, ZeroTimeoutMeansDefault) {
    ASSERT_EQ(Send(0U, 100U), XGL_OK);
    ExpectFirstRetry(100U, 200U);
}

TEST_F(XglTimeoutControlTest, InPlaceSendDoesNotScheduleReliableRetry) {
    uint8_t buffer[128] = {};
    buffer[XGL_FRAME_HEADER_SIZE] = payload;
    xgl_tx_zerocopy_request_t request = {};
    request.buffer = buffer;
    request.buffer_size = sizeof(buffer);
    request.data_offset = XGL_FRAME_HEADER_SIZE;
    request.data_len = 1U;
    request.target_id = 2U;

    ASSERT_EQ(xgl_send_zerocopy_scoped_at(instance, &request, 100U), XGL_OK);
    EXPECT_EQ(data_calls, 1U);
    Step(101U);
    Step(100000U);
    EXPECT_EQ(data_calls, 1U);

    EXPECT_EQ(xgl_send_zerocopy_scoped_at(instance, &request, 100001U), XGL_OK);
    EXPECT_EQ(data_calls, 2U);
}

TEST_F(XglTimeoutControlTest, VariousTimeoutValues) {
    const uint32_t timeouts[] = {0U, 1U, 100U, 10000U,
                                 static_cast<uint32_t>(INT32_MAX)};
    const uint32_t start = UINT32_MAX - 31U;
    for (const uint32_t timeout : timeouts) {
        SCOPED_TRACE(timeout);
        CreateInstance();
        ASSERT_EQ(Send(timeout, start), XGL_OK);
        ExpectFirstRetry(start, timeout == 0U ? 200U : timeout);
    }

    CreateInstance();
    EXPECT_EQ(Send(static_cast<uint32_t>(INT32_MAX) + 1U, start),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(data_calls, 0U);
    ASSERT_EQ(Send(100U, start), XGL_OK);
    ExpectFirstRetry(start, 100U);
}
