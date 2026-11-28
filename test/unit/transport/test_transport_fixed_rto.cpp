/**
 * \file            test_transport_fixed_rto.cpp
 * \brief           Fixed retransmission timeout selection and backoff
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-02
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include "test_host_allocator.h"
#include <transport/xgl_transport.h>
#include <wire/xgl_wire.h>

#include <array>
#include <gtest/gtest.h>

#if !XGL_FEATURE_ADAPTIVE_RTO
namespace {

class XglTransportFixedRtoTest : public ::testing::Test {
  protected:
    xgl_transport_ctx_t context = {};
    xgl_layer_stats_t statistics = {};
    xgl_packet_tx_endpoint_t lower = {};
    unsigned data_sends = 0U;

    static xgl_error_t SendPacket(void* user, xgl_handle_t,
                                  xgl_packet_t* packet) {
        if (packet->packet_type == XGL_PACKET_TYPE_DATA) {
            ++static_cast<XglTransportFixedRtoTest*>(user)->data_sends;
        }
        return XGL_OK;
    }

    void SetUp() override {
        lower = {this, SendPacket, nullptr, nullptr};
        xgl_transport_config_t config = {};
        config.allocator = xgl_test_host_allocator();
        config.local_id = 1U;
        config.max_retry_count = 3U;
        config.default_timeout_ms = 700U;
        config.window_size = 1U;
        config.max_frame_size = 128U;
        config.max_peers = 1U;
        config.max_tx_packets = 1U;
        config.stats = &statistics;
        config.lower_layer = &lower;
        ASSERT_EQ(xgl_transport_init(&context, &config), XGL_OK);
    }

    void TearDown() override {
        xgl_transport_destroy(&context);
    }

    xgl_error_t Send(uint32_t timeout, uint32_t now) {
        context.current_time_ms = now;
        const uint8_t payload = 0x42U;
        xgl_tx_data_t request = {};
        request.target_id = 2U;
        request.data = &payload;
        request.data_len = 1U;
        request.reliable = true;
        request.timeout_ms = timeout;
        return xgl_transport_send(&context, nullptr, &request);
    }

    xgl_error_t Acknowledge(uint32_t now) {
        std::array<uint8_t, 32> value = {}, extensions = {};
        size_t value_length = 0U, extension_length = 0U;
        const xgl_wire_ack_range_t range = {0U, 1U};
        xgl_error_t error = xgl_wire_encode_ack_range_ext_value(
            value.data(), value.size(), 0U, 0U, &range, 1U, &value_length);
        if (error != XGL_OK) {
            return error;
        }
        error = xgl_wire_encode_ext(extensions.data(), extensions.size(),
                                    XGL_WIRE_EXT_ACK_RANGE, value.data(),
                                    value_length, &extension_length);
        if (error != XGL_OK) {
            return error;
        }
        context.current_time_ms = now;
        xgl_packet_t packet = {};
        packet.source_id = 2U;
        packet.target_id = 1U;
        packet.packet_type = XGL_PACKET_TYPE_ACK;
        packet.reliable = XGL_RELIABILITY_ACK_ONLY;
        packet.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
        packet.extensions = extensions.data();
        packet.extensions_len = extension_length;
        return xgl_transport_receive(&context, nullptr, &packet);
    }

    void ExpectDeadline(uint32_t now, uint32_t expected) {
        uint32_t remaining = UINT32_MAX;
        ASSERT_TRUE(xgl_transport_next_timeout(&context, now, &remaining));
        EXPECT_EQ(remaining, expected);
    }
};

TEST_F(XglTransportFixedRtoTest, ZeroRequestTimeoutUsesConfiguredDefault) {
    ASSERT_EQ(Send(0U, 100U), XGL_OK);
    ExpectDeadline(100U, 700U);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 799U), XGL_OK);
    EXPECT_EQ(data_sends, 1U);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 800U), XGL_OK);
    EXPECT_EQ(data_sends, 2U);
}

TEST_F(XglTransportFixedRtoTest,
       AcknowledgmentDoesNotReplaceConfiguredDefault) {
    ASSERT_EQ(Send(250U, 100U), XGL_OK);
    ASSERT_EQ(Acknowledge(150U), XGL_OK);
    ASSERT_EQ(Send(0U, 200U), XGL_OK);
    ExpectDeadline(200U, 700U);
}

TEST_F(XglTransportFixedRtoTest, RequestOverrideRemainsEffectiveAfterFeedback) {
    ASSERT_EQ(Send(250U, 100U), XGL_OK);
    ASSERT_EQ(Acknowledge(150U), XGL_OK);
    ASSERT_EQ(Send(450U, 200U), XGL_OK);
    ExpectDeadline(200U, 450U);
}

TEST_F(XglTransportFixedRtoTest, SuccessfulRetryRetainsExponentialBackoff) {
    ASSERT_EQ(Send(0U, 100U), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 800U), XGL_OK);
    EXPECT_EQ(data_sends, 2U);
    ExpectDeadline(800U, 1400U);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 2199U), XGL_OK);
    EXPECT_EQ(data_sends, 2U);
    ASSERT_EQ(xgl_transport_run(&context, nullptr, 2200U), XGL_OK);
    EXPECT_EQ(data_sends, 3U);
    ExpectDeadline(2200U, 2800U);
}

} /* namespace */
#endif
