/**
 * \file            test_network_contracts.cpp
 * \brief           Validated receive boundaries across protocol profiles
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <gtest/gtest.h>
#include <network/xgl_network.h>
#include <wire/xgl_frame.h>
#include <wire/xgl_wire.h>

namespace {

class XglNetworkContracts : public ::testing::Test {
  protected:
    void SetUp() override {
        xgl_frame_params_t params = {};
        params.source_id = 2U;
        params.target_id = 1U;
        params.ttl = 3U;
        params.payload = payload;
        params.payload_len = sizeof(payload);
        xgl_frame_t frame = {};
        ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
        ASSERT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &length),
                  XGL_OK);
        ASSERT_EQ(xgl_wire_decode_frame(&view, bytes, length, nullptr), XGL_OK);
        network.local_id = 1U;
        network.max_frame_size = sizeof(bytes);
        ASSERT_EQ(xgl_network_get_endpoints(&network, &packets, &frames),
                  XGL_OK);
    }

    const uint8_t payload[3] = {0x12U, 0x34U, 0x56U};
    uint8_t bytes[64] = {};
    size_t length = 0U;
    xgl_wire_frame_view_t view = {};
    xgl_network_ctx_t network = {};
    xgl_packet_tx_endpoint_t packets = {};
    xgl_frame_rx_endpoint_t frames = {};
};

TEST_F(XglNetworkContracts, RejectsReservedAndSelfAddresses) {
    EXPECT_FALSE(xgl_network_validate_address(&network, 0U, 2U));
    EXPECT_FALSE(xgl_network_validate_address(&network, 1U, 0U));
    EXPECT_FALSE(xgl_network_validate_address(&network, 1U, 0xFFFFU));
    EXPECT_FALSE(xgl_network_validate_address(&network, 1U, 1U));
    EXPECT_TRUE(xgl_network_validate_address(&network, 1U, 2U));
    EXPECT_TRUE(xgl_network_validate_address(&network, 0xFFFFU, 2U));
}

TEST_F(XglNetworkContracts, EndpointRequiresValidatedView) {
    xgl_frame_rx_message_t message = {};
    EXPECT_EQ(frames.receive(frames.ctx, nullptr, &message),
              XGL_ERR_NULL_POINTER);
    message.view = &view;
    EXPECT_EQ(frames.receive(frames.ctx, nullptr, &message), XGL_OK);
    EXPECT_EQ(xgl_network_receive(&network, nullptr, bytes, length), XGL_OK);
}

TEST_F(XglNetworkContracts, RawReceiveEnforcesInstanceLimitBeforeDelivery) {
    size_t deliveries = 0U;
    xgl_packet_rx_endpoint_t receiver = {};
    receiver.ctx = &deliveries;
    receiver.receive = [](void* context, xgl_handle_t, const xgl_packet_t*) {
        ++*static_cast<size_t*>(context);
        return XGL_OK;
    };
    network.upper_layer = &receiver;
    network.max_frame_size = length - 1U;
    EXPECT_EQ(xgl_network_receive(&network, nullptr, bytes, length),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(deliveries, 0U);
    network.max_frame_size = length;
    EXPECT_EQ(xgl_network_receive(&network, nullptr, bytes, length), XGL_OK);
    EXPECT_EQ(deliveries, 1U);
}

TEST_F(XglNetworkContracts, ViewDeliveryReturnsUpperAcceptanceUnchanged) {
    xgl_error_t result = XGL_ERR_BUSY;
    xgl_packet_rx_endpoint_t receiver = {};
    receiver.ctx = &result;
    receiver.receive = [](void* context, xgl_handle_t,
                          const xgl_packet_t* packet) {
        EXPECT_EQ(packet->source_id, 2U);
        EXPECT_EQ(packet->target_id, 1U);
        EXPECT_EQ(packet->data->data_len, 3U);
        return *static_cast<xgl_error_t*>(context);
    };
    network.upper_layer = &receiver;
    EXPECT_EQ(xgl_network_receive_view(&network, nullptr, &view), XGL_ERR_BUSY);
    result = XGL_OK;
    EXPECT_EQ(xgl_network_receive_view(&network, nullptr, &view), XGL_OK);
}

} /* namespace */
