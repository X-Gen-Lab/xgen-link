/**
 * \file            test_boot_network.cpp
 * \brief           Boot logical TX bounds retain the complete wire decoder
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <datalink/xgl_datalink.h>
#include <network/xgl_network.h>
#include <wire/xgl_frame.h>
#include <xgl/xgl_alloc.h>

#include <array>
#include <cstring>
#include <gtest/gtest.h>

class XglBootNetworkTest : public ::testing::Test {
  protected:
    void SetUp() override {
        allocator.ctx = this;
        allocator.alloc = [](void* user, size_t size) -> void* {
            auto* self = static_cast<XglBootNetworkTest*>(user);
            if (self->leased || size > self->scratch.size()) {
                return nullptr;
            }
            self->leased = true;
            return self->scratch.data();
        };
        allocator.free = [](void* user, void*) {
            static_cast<XglBootNetworkTest*>(user)->leased = false;
        };
        phy.user_data = this;
        phy.tx = [](const uint8_t* bytes, size_t length, void* user) {
            auto* self = static_cast<XglBootNetworkTest*>(user);
            ++self->transmissions;
            if (length > self->captured.size()) {
                return XGL_ERR_BUFFER_TOO_SMALL;
            }
            std::memcpy(self->captured.data(), bytes, length);
            self->captured_length = length;
            return XGL_OK;
        };
        route.target_id = 2U;
        route.phy = &phy;
        route.max_frame_size = 128U;
        routes.routes = &route;
        routes.route_count = 1U;
        routes.route_capacity = 1U;
        xgl_datalink_config_t datalink_config = {};
        datalink_config.allocator = &allocator;
        ASSERT_EQ(xgl_datalink_init(&datalink, &datalink_config), XGL_OK);
        ASSERT_EQ(xgl_datalink_get_tx_endpoint(&datalink, &lower), XGL_OK);
        xgl_network_config_t network_config = {};
        network_config.local_id = 1U;
        network_config.route_table = &routes;
        network_config.lower_layer = &lower;
        ASSERT_EQ(xgl_network_init(&network, &network_config), XGL_OK);
        packet.target_id = 2U;
        packet.data = &payload;
    }

    xgl_allocator_t allocator = {};
    std::array<uint8_t, 128> scratch{}, captured{};
    bool leased = false;
    unsigned transmissions = 0U;
    size_t captured_length = 0U;
    xgl_phy_ops_t phy = {};
    xgl_route_item_t route = {};
    xgl_route_table_t routes = {};
    xgl_datalink_ctx_t datalink = {};
    xgl_frame_tx_endpoint_t lower = {};
    xgl_network_ctx_t network = {};
    xgl_packet_data_t payload = {};
    xgl_packet_t packet = {};
};

TEST_F(XglBootNetworkTest, ComposesAckSessionAndDataTypeWithin32Bytes) {
    uint8_t value[13] = {};
    xgl_wire_ack_range_t range = {0U, 1U};
    size_t value_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(value, sizeof(value), 8U, 0U,
                                                  &range, 1U, &value_len),
              XGL_OK);
    uint8_t extension[15] = {};
    size_t extension_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(extension, sizeof(extension),
                                  XGL_WIRE_EXT_ACK_RANGE, value, value_len,
                                  &extension_len),
              XGL_OK);
    packet.packet_type = XGL_PACKET_TYPE_ACK;
    packet.reliable = XGL_RELIABILITY_ACK_ONLY;
    packet.data_type = 9U;
    packet.connection_id = 11U;
    packet.session_epoch = 7U;
    packet.extensions = extension;
    packet.extensions_len = extension_len;
    ASSERT_EQ(xgl_network_send(&network, &packet), XGL_OK);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(
        xgl_wire_decode_frame(&view, captured.data(), captured_length, nullptr),
        XGL_OK);
    EXPECT_EQ(view.extensions_len, 32U);
    EXPECT_EQ(view.header.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(view.header.connection_id, 11U);
    EXPECT_EQ(view.session_epoch, 7U);
    EXPECT_EQ(view.data_type, 9U);
    EXPECT_EQ(transmissions, 1U);
    EXPECT_FALSE(leased);
}

TEST_F(XglBootNetworkTest, RejectsLogicalTxBeyondTheBootExtensionWorkspace) {
    uint8_t extension[33] = {0x70U, 31U};
    packet.extensions = extension;
    packet.extensions_len = sizeof(extension);
    EXPECT_EQ(xgl_network_send(&network, &packet), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(transmissions, 0U);
    EXPECT_FALSE(leased);
}

TEST_F(XglBootNetworkTest, WireStillPreservesLongUnknownExtensions) {
    uint8_t extension[33] = {0x70U, 31U};
    xgl_frame_params_t params = {};
    params.source_id = 1U;
    params.target_id = 2U;
    params.extensions = extension;
    params.extensions_len = sizeof(extension);
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    ASSERT_EQ(xgl_frame_serialize(captured.data(), captured.size(), &frame,
                                  &captured_length),
              XGL_OK);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(
        xgl_wire_decode_frame(&view, captured.data(), captured_length, nullptr),
        XGL_OK);
    ASSERT_EQ(view.extensions_len, sizeof(extension));
    EXPECT_EQ(std::memcmp(view.extensions, extension, sizeof(extension)), 0);
}
