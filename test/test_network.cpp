/**
 * \file            test_network.cpp
 * \brief           Unit tests for network layer
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_network.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_wire.h>
#include <cstring>
#include <vector>

/*---------------------------------------------------------------------------*/
/* Test PHY Operations                                                       */
/*---------------------------------------------------------------------------*/

static xgl_error_t test_phy_tx(const uint8_t* data, size_t len, void* user_data) {
    (void)data;
    (void)len;
    int* count = (int*)user_data;
    if (count) (*count)++;
    return XGL_OK;
}

static xgl_error_t test_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    (void)buffer;
    (void)user_data;
    *len = 0;
    return XGL_OK;
}

struct CaptureTx {
    int count = 0;
    std::vector<uint8_t> bytes;
};

static xgl_error_t capture_phy_tx(const uint8_t* data, size_t len, void* user_data) {
    auto* capture = static_cast<CaptureTx*>(user_data);
    if (capture != nullptr) {
        capture->count++;
        capture->bytes.assign(data, data + len);
    }
    return XGL_OK;
}

static xgl_error_t network_test_auth_sign(uint32_t key_id,
                                          const uint8_t* aad,
                                          size_t aad_len,
                                          const uint8_t* payload,
                                          size_t payload_len,
                                          uint8_t* tag,
                                          size_t tag_capacity,
                                          size_t* tag_len,
                                          void* user_data) {
    (void)user_data;
    if (tag == nullptr || tag_len == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    if ((aad == nullptr && aad_len > 0U) ||
        (payload == nullptr && payload_len > 0U)) {
        return XGL_ERR_NULL_POINTER;
    }
    if (tag_capacity < 8U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint32_t acc = key_id ^ 0xA5A5A5A5U;
    for (size_t i = 0; i < aad_len; ++i) {
        acc = (acc * 31U) ^ aad[i];
    }
    for (size_t i = 0; i < payload_len; ++i) {
        acc = (acc * 31U) ^ payload[i];
    }
    for (size_t i = 0; i < 8U; ++i) {
        tag[i] = static_cast<uint8_t>((acc >> ((i % 4U) * 8U)) & 0xFFU);
    }
    *tag_len = 8U;
    return XGL_OK;
}

static xgl_error_t network_test_auth_verify(uint32_t key_id,
                                            const uint8_t* aad,
                                            size_t aad_len,
                                            const uint8_t* payload,
                                            size_t payload_len,
                                            const uint8_t* tag,
                                            size_t tag_len,
                                            bool* valid,
                                            void* user_data) {
    if (tag == nullptr || valid == nullptr) {
        return XGL_ERR_NULL_POINTER;
    }
    uint8_t expected[8] = {};
    size_t expected_len = 0;
    xgl_error_t err = network_test_auth_sign(key_id,
                                             aad,
                                             aad_len,
                                             payload,
                                             payload_len,
                                             expected,
                                             sizeof(expected),
                                             &expected_len,
                                             user_data);
    if (err != XGL_OK) {
        return err;
    }
    *valid = tag_len == expected_len &&
             std::memcmp(tag, expected, expected_len) == 0;
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglNetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Initialize route table */
        xgl_route_table_init(&route_table, 4, nullptr);
        
        /* Initialize statistics */
        memset(&stats, 0, sizeof(stats));
        
        /* Initialize network context */
        xgl_network_config_t config = {
            .local_id = LOCAL_ID,
            .route_table = &route_table,
            .upper_layer = nullptr,
            .lower_layer = nullptr,
            .error_callback = nullptr,
            .callback_user_data = nullptr,
            .stats = &stats
        };
        xgl_network_init(&network_ctx, &config);
        
        /* Initialize PHY operations */
        phy_tx_count = 0;
        phy_ops.tx = test_phy_tx;
        phy_ops.rx = test_phy_rx;
        phy_ops.user_data = &phy_tx_count;
    }
    
    void TearDown() override {
        xgl_route_table_destroy(&route_table);
    }
    
    static constexpr uint8_t LOCAL_ID = 1;
    static constexpr uint8_t REMOTE_ID = 2;
    static constexpr uint8_t FORWARD_ID = 3;

    std::vector<uint8_t> make_frame(uint16_t source_id,
                                    uint16_t target_id,
                                    uint8_t ttl = XGL_DEFAULT_TTL,
                                    const char* payload = "hello") {
        xgl_frame_t frame = {};
        xgl_frame_params_t params = {
            .source_id = source_id,
            .target_id = target_id,
            .data_type = 1,
            .payload = reinterpret_cast<const uint8_t*>(payload),
            .payload_len = std::strlen(payload),
            .reliable = true,
            .reliability_class = XGL_RELIABILITY_NONE,
            .fragment = false,
            .priority = 0,
            .session_id = 0,
            .ttl = ttl
        };
        EXPECT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

        std::vector<uint8_t> bytes(xgl_frame_calculate_size(params.payload_len));
        size_t written = 0;
        EXPECT_EQ(xgl_frame_serialize(bytes.data(), bytes.size(), &frame, &written), XGL_OK);
        bytes.resize(written);
        return bytes;
    }
    
    xgl_route_table_t route_table;
    xgl_network_ctx_t network_ctx;
    xgl_layer_stats_t stats;
    xgl_phy_ops_t phy_ops;
    int phy_tx_count;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, InitializeNetworkContext) {
    xgl_network_ctx_t ctx;
    xgl_route_table_t table;
    xgl_layer_stats_t test_stats = {0};
    
    xgl_route_table_init(&table, 4, nullptr);
    
    xgl_network_config_t config = {
        .local_id = 1,
        .route_table = &table,
        .upper_layer = nullptr,
        .lower_layer = nullptr,
        .error_callback = nullptr,
        .callback_user_data = nullptr,
        .stats = &test_stats
    };
    xgl_error_t err = xgl_network_init(&ctx, &config);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(ctx.local_id, 1);
    EXPECT_EQ(ctx.route_table, &table);
    
    xgl_route_table_destroy(&table);
}

/*---------------------------------------------------------------------------*/
/* Route Table Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, AddRoute) {
    xgl_error_t err = xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops,
                                         256, 100, 1);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_route_item_t* found = xgl_route_table_lookup(&route_table, REMOTE_ID);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->target_id, REMOTE_ID);
}

TEST_F(XglNetworkTest, RemoveRoute) {
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);
    
    xgl_error_t err = xgl_route_table_remove(&route_table, REMOTE_ID);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_route_item_t* found = xgl_route_table_lookup(&route_table, REMOTE_ID);
    EXPECT_EQ(found, nullptr);
}

TEST_F(XglNetworkTest, LookupNonexistentRoute) {
    xgl_route_item_t* found = xgl_route_table_lookup(&route_table, 99);
    EXPECT_EQ(found, nullptr);
}

/*---------------------------------------------------------------------------*/
/* Statistics Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, StatisticsInitiallyZero) {
    EXPECT_EQ(stats.tx_packets, 0);
    EXPECT_EQ(stats.tx_bytes, 0);
    EXPECT_EQ(stats.rx_packets, 0);
    EXPECT_EQ(stats.rx_bytes, 0);
}

/*---------------------------------------------------------------------------*/
/* Address Validation Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ValidateValidAddress) {
    bool valid = xgl_network_validate_address(&network_ctx, REMOTE_ID, LOCAL_ID);
    EXPECT_TRUE(valid);
}

TEST_F(XglNetworkTest, ValidateBroadcastSourceInvalid) {
    bool valid = xgl_network_validate_address(&network_ctx, LOCAL_ID, XGL_BROADCAST_ID);
    EXPECT_FALSE(valid);
}

TEST_F(XglNetworkTest, ValidateZeroSourceInvalid) {
    bool valid = xgl_network_validate_address(&network_ctx, LOCAL_ID, 0);
    EXPECT_FALSE(valid);
}

TEST_F(XglNetworkTest, ValidateBroadcastTargetValid) {
    bool valid = xgl_network_validate_address(&network_ctx, XGL_BROADCAST_ID, LOCAL_ID);
    EXPECT_TRUE(valid);
}

TEST_F(XglNetworkTest, ValidateSelfAddressInvalid) {
    bool valid = xgl_network_validate_address(&network_ctx, LOCAL_ID, LOCAL_ID);
    EXPECT_FALSE(valid);
}

/*---------------------------------------------------------------------------*/
/* Local Node Detection Tests                                                */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, IsLocalForLocalId) {
    bool is_local = xgl_network_is_local(&network_ctx, LOCAL_ID);
    EXPECT_TRUE(is_local);
}

TEST_F(XglNetworkTest, IsLocalForBroadcast) {
    bool is_local = xgl_network_is_local(&network_ctx, XGL_BROADCAST_ID);
    EXPECT_TRUE(is_local);
}

TEST_F(XglNetworkTest, IsNotLocalForRemoteId) {
    bool is_local = xgl_network_is_local(&network_ctx, REMOTE_ID);
    EXPECT_FALSE(is_local);
}

/*---------------------------------------------------------------------------*/
/* Send Packet Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, SendPacketWithoutRoute) {
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = 10,
        .data = (uint8_t*)"test_data",
        .owned_data = nullptr
    };
    
    xgl_packet_t packet = {
        .source_id = LOCAL_ID,
        .target_id = REMOTE_ID,
        .data_type = 1,
        .reliable = 1,
        .priority = 0,
        .data = &packet_data
    };
    
    xgl_error_t err = xgl_network_send(&network_ctx, &packet, false);
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
}

TEST_F(XglNetworkTest, SendPacketWithRoute) {
    /* Add route */
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);
    
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = 10,
        .data = (uint8_t*)"test_data",
        .owned_data = nullptr
    };
    
    xgl_packet_t packet = {
        .source_id = LOCAL_ID,
        .target_id = REMOTE_ID,
        .data_type = 1,
        .reliable = 1,
        .priority = 0,
        .data = &packet_data
    };
    
    /* Mock lower layer interface */
    xgl_layer_interface_t lower_layer;
    lower_layer.ctx = nullptr;
    lower_layer.send = [](void* ctx, xgl_handle_t handle, void* data) -> xgl_error_t {
        (void)ctx;
        (void)handle;
        (void)data;
        return XGL_OK;
    };
    lower_layer.receive = nullptr;
    lower_layer.report_error = nullptr;
    
    network_ctx.lower_layer = &lower_layer;
    
    xgl_error_t err = xgl_network_send(&network_ctx, &packet, false);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 1);
}

TEST_F(XglNetworkTest, SendPacketEncodesApplicationTypeAsHeaderExtension) {
    constexpr uint8_t kAppTypeThatCollidesWithAck = XGL_PACKET_TYPE_ACK;
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);

    const uint8_t payload[] = {'a', 'p', 'p'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };

    struct CapturedFrameMessage {
        xgl_frame_t frame = {};
        std::vector<uint8_t> extensions;
    } capture;
    xgl_layer_interface_t lower_layer = {};
    lower_layer.ctx = &capture;
    lower_layer.send = [](void* ctx, xgl_handle_t handle, void* data) -> xgl_error_t {
        (void)handle;
        auto* capture = static_cast<CapturedFrameMessage*>(ctx);
        auto* message = static_cast<xgl_frame_tx_message_t*>(data);
        if (capture == nullptr || message == nullptr || message->frame == nullptr) {
            return XGL_ERR_NULL_POINTER;
        }
        capture->frame = *message->frame;
        if (message->frame->extensions != nullptr && message->frame->extensions_len > 0U) {
            capture->extensions.assign(message->frame->extensions,
                                       message->frame->extensions +
                                           message->frame->extensions_len);
            capture->frame.extensions = capture->extensions.data();
        }
        return XGL_OK;
    };
    network_ctx.lower_layer = &lower_layer;

    xgl_packet_t packet = {
        .source_id = LOCAL_ID,
        .target_id = REMOTE_ID,
        .data_type = kAppTypeThatCollidesWithAck,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .priority = 4,
        .data = &packet_data
    };

    ASSERT_EQ(xgl_network_send(&network_ctx, &packet, false), XGL_OK);
    EXPECT_EQ(capture.frame.header.packet_type, XGL_PACKET_TYPE_DATA);
    EXPECT_NE(capture.frame.header.flags & XGL_WIRE_FLAG_HAS_EXTENSIONS, 0U);
    ASSERT_NE(capture.frame.extensions, nullptr);

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor,
                                       capture.extensions.data(),
                                       capture.extensions.size()),
              XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_DATA_TYPE);
    ASSERT_EQ(ext.len, 1U);
    EXPECT_EQ(ext.value[0], kAppTypeThatCollidesWithAck);
}

TEST_F(XglNetworkTest, SendPacketRejectsConflictingDataTypeExtension) {
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 100, 1);

    uint8_t ext_buf[XGL_DATA_TYPE_EXT_SIZE] = {};
    uint8_t ext_data_type = 7U;
    size_t ext_len = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(ext_buf,
                                  sizeof(ext_buf),
                                  XGL_WIRE_EXT_DATA_TYPE,
                                  &ext_data_type,
                                  1U,
                                  &ext_len),
              XGL_OK);

    const uint8_t payload[] = {'a'};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = LOCAL_ID,
        .target_id = REMOTE_ID,
        .data_type = 9U,
        .reliable = XGL_RELIABILITY_ACK_ELICITING,
        .data = &packet_data,
        .extensions = ext_buf,
        .extensions_len = ext_len
    };

    EXPECT_EQ(xgl_network_send(&network_ctx, &packet, false), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglNetworkTest, SendPacketRouteMtuIncludesAuthenticationOverhead) {
    xgl_auth_provider_t provider = {
        .sign = network_test_auth_sign,
        .verify = network_test_auth_verify,
        .tag_len = 8,
        .user_data = nullptr
    };
    network_ctx.auth_required = true;
    network_ctx.auth_key_id = 7;
    network_ctx.auth_provider = &provider;
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 60, 100, 1);

    const uint8_t payload[20] = {};
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = sizeof(payload),
        .data = payload,
        .owned_data = nullptr
    };
    xgl_packet_t packet = {
        .source_id = LOCAL_ID,
        .target_id = REMOTE_ID,
        .data_type = 1,
        .reliable = XGL_RELIABILITY_NONE,
        .priority = 0,
        .data = &packet_data
    };

    bool lower_called = false;
    xgl_layer_interface_t lower_layer = {};
    lower_layer.ctx = &lower_called;
    lower_layer.send = [](void* ctx, xgl_handle_t handle, void* data) -> xgl_error_t {
        (void)handle;
        (void)data;
        *static_cast<bool*>(ctx) = true;
        return XGL_OK;
    };
    network_ctx.lower_layer = &lower_layer;

    EXPECT_EQ(xgl_network_send(&network_ctx, &packet, false), XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_FALSE(lower_called);
}

TEST_F(XglNetworkTest, SendPacketNullPointer) {
    xgl_error_t err = xgl_network_send(nullptr, nullptr, false);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Receive Packet Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ReceivePacketForLocalNode) {
    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, LOCAL_ID);
    
    /* Mock upper layer interface */
    bool upper_called = false;
    xgl_layer_interface_t upper_layer;
    upper_layer.ctx = &upper_called;
    upper_layer.receive = [](void* ctx, xgl_handle_t handle, void* data) -> xgl_error_t {
        (void)handle;
        (void)data;
        bool* called = (bool*)ctx;
        *called = true;
        return XGL_OK;
    };
    upper_layer.send = nullptr;
    upper_layer.report_error = nullptr;
    
    network_ctx.upper_layer = &upper_layer;
    
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_buf.size());
    EXPECT_EQ(err, XGL_OK);
    EXPECT_TRUE(upper_called);
    EXPECT_EQ(stats.rx_packets, 1);
}

TEST_F(XglNetworkTest, ReceivePacketForForwarding) {
    /* Add route for forwarding */
    xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1);
    
    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, FORWARD_ID);
    
    int initial_tx_count = phy_tx_count;
    
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_buf.size());
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(phy_tx_count, initial_tx_count + 1);  // Should forward
}

TEST_F(XglNetworkTest, ForwardingUsesTargetRouteEgressPhy) {
    int first_phy_count = 0;
    int second_phy_count = 0;
    xgl_phy_ops_t first_phy = {
        .tx = test_phy_tx,
        .rx = test_phy_rx,
        .user_data = &first_phy_count
    };
    xgl_phy_ops_t second_phy = {
        .tx = test_phy_tx,
        .rx = test_phy_rx,
        .user_data = &second_phy_count
    };

    ASSERT_EQ(xgl_route_table_add(&route_table, 4, &first_phy, 256, 100, 10), XGL_OK);
    ASSERT_EQ(xgl_route_table_add(&route_table, 5, &second_phy, 256, 100, 1), XGL_OK);

    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, 5);

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_buf.size()), XGL_OK);
    EXPECT_EQ(first_phy_count, 0);
    EXPECT_EQ(second_phy_count, 1);
    EXPECT_EQ(stats.tx_packets, 1);
}

TEST_F(XglNetworkTest, ForwardingDropsExpiredTtl) {
    ASSERT_EQ(xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1), XGL_OK);

    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, FORWARD_ID, 0);

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_buf.size()), XGL_ERR_TTL_EXPIRED);
    EXPECT_EQ(phy_tx_count, 0);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ForwardingDropsPacketWithTtlOneBeforeForwarding) {
    ASSERT_EQ(xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1), XGL_OK);

    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, FORWARD_ID, 1);

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_buf.size()),
              XGL_ERR_TTL_EXPIRED);
    EXPECT_EQ(phy_tx_count, 0);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ForwardingRejectsRouteMtuOverflow) {
    ASSERT_EQ(xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 32, 100, 1), XGL_OK);

    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, FORWARD_ID, XGL_DEFAULT_TTL,
                                                "payload-larger-than-route-mtu");
    ASSERT_GT(frame_buf.size(), 32U);

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_buf.size()),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(phy_tx_count, 0);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ForwardingResignsAuthenticatedFrameAfterTtlDecrement) {
    xgl_auth_provider_t provider = {
        .sign = network_test_auth_sign,
        .verify = network_test_auth_verify,
        .tag_len = 8,
        .user_data = nullptr
    };
    network_ctx.auth_required = true;
    network_ctx.auth_key_id = 7;
    network_ctx.auth_provider = &provider;

    CaptureTx capture;
    xgl_phy_ops_t capture_phy = {
        .tx = capture_phy_tx,
        .rx = test_phy_rx,
        .user_data = &capture
    };
    ASSERT_EQ(xgl_route_table_add(&route_table, FORWARD_ID, &capture_phy, 256, 100, 1),
              XGL_OK);

    const char payload[] = "signed-hop";
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {
        .source_id = REMOTE_ID,
        .target_id = FORWARD_ID,
        .data_type = 1,
        .payload = reinterpret_cast<const uint8_t*>(payload),
        .payload_len = sizeof(payload) - 1U,
        .reliable = true,
        .priority = 0,
        .ttl = 4
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> encoded(256);
    size_t encoded_len = 0;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded.data(),
                                                encoded.size(),
                                                &frame,
                                                7,
                                                &provider,
                                                &encoded_len),
              XGL_OK);
    encoded.resize(encoded_len);

    ASSERT_EQ(xgl_network_receive(&network_ctx, nullptr, encoded.data(), encoded.size()),
              XGL_OK);
    ASSERT_EQ(capture.count, 1);
    ASSERT_FALSE(capture.bytes.empty());

    xgl_wire_header_t forwarded = {};
    ASSERT_EQ(xgl_wire_decode_header(&forwarded,
                                     capture.bytes.data(),
                                     capture.bytes.size()),
              XGL_OK);
    EXPECT_EQ(forwarded.ttl, 3U);

    bool valid = false;
    EXPECT_EQ(xgl_wire_verify_auth_trailer(capture.bytes.data(),
                                           capture.bytes.size() - XGL_CRC16_SIZE,
                                           forwarded.header_len,
                                           forwarded.payload_len,
                                           7,
                                           &provider,
                                           &valid),
              XGL_OK);
    EXPECT_TRUE(valid);
}

TEST_F(XglNetworkTest, ReceivePacketNoRouteForForwarding) {
    std::vector<uint8_t> frame_buf = make_frame(REMOTE_ID, 99);

    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf.data(), frame_buf.size());
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ReceiveInvalidFrame) {
    uint8_t frame_buf[5] = {0};  // Too short
    
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf, 5);
    EXPECT_EQ(err, XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(stats.rx_errors, 1);
}

TEST_F(XglNetworkTest, ReceiveRejectsDuplicateDataTypeExtension) {
    uint8_t ext_buf[XGL_DATA_TYPE_EXT_SIZE * 2U] = {};
    uint8_t data_type_a = 3U;
    uint8_t data_type_b = 4U;
    size_t ext_len = 0U;
    size_t written = 0U;
    ASSERT_EQ(xgl_wire_encode_ext(ext_buf,
                                  sizeof(ext_buf),
                                  XGL_WIRE_EXT_DATA_TYPE,
                                  &data_type_a,
                                  1U,
                                  &written),
              XGL_OK);
    ext_len += written;
    ASSERT_EQ(xgl_wire_encode_ext(&ext_buf[ext_len],
                                  sizeof(ext_buf) - ext_len,
                                  XGL_WIRE_EXT_DATA_TYPE,
                                  &data_type_b,
                                  1U,
                                  &written),
              XGL_OK);
    ext_len += written;

    const uint8_t payload[] = {'x'};
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {
        .source_id = REMOTE_ID,
        .target_id = LOCAL_ID,
        .packet_type = XGL_PACKET_TYPE_DATA,
        .extensions = ext_buf,
        .extensions_len = ext_len,
        .payload = payload,
        .payload_len = sizeof(payload),
        .ttl = XGL_DEFAULT_TTL
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> frame_buf(xgl_frame_calculate_size(sizeof(payload)) + ext_len);
    size_t frame_len = 0U;
    ASSERT_EQ(xgl_frame_serialize(frame_buf.data(),
                                  frame_buf.size(),
                                  &frame,
                                  &frame_len),
              XGL_OK);

    EXPECT_EQ(xgl_network_receive(&network_ctx,
                                  nullptr,
                                  frame_buf.data(),
                                  frame_len),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(stats.rx_errors, 1);
}

/*---------------------------------------------------------------------------*/
/* Error Callback Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ErrorCallbackInvoked) {
    bool callback_invoked = false;
    xgl_error_t callback_error = XGL_OK;
    
    auto error_cb = [](xgl_handle_t handle, xgl_error_t error, 
                       const char* message, void* user_data) {
        (void)handle;
        (void)message;
        bool* invoked = (bool*)user_data;
        *invoked = true;
    };
    
    network_ctx.error_callback = error_cb;
    network_ctx.callback_user_data = &callback_invoked;
    
    xgl_packet_data_t packet_data = {
        .ref_count = 1,
        .data_len = 10,
        .data = (uint8_t*)"test_data",
        .owned_data = nullptr
    };
    
    xgl_packet_t packet = {
        .source_id = LOCAL_ID,
        .target_id = 99,  // No route
        .data_type = 1,
        .reliable = 1,
        .priority = 0,
        .data = &packet_data
    };
    
    xgl_network_send(&network_ctx, &packet, false);
    EXPECT_TRUE(callback_invoked);
}

/*---------------------------------------------------------------------------*/
/* Layer Interface Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, GetLayerInterface) {
    xgl_layer_interface_t iface;
    
    xgl_error_t err = xgl_network_get_interface(&network_ctx, &iface);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(iface.ctx, &network_ctx);
    EXPECT_NE(iface.send, nullptr);
    EXPECT_NE(iface.receive, nullptr);
    EXPECT_NE(iface.report_error, nullptr);
}

TEST_F(XglNetworkTest, GetLayerInterfaceNullPointer) {
    xgl_error_t err = xgl_network_get_interface(nullptr, nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}
