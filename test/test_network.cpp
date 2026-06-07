/**
 * \file            test_network.cpp
 * \brief           Unit tests for network layer
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_network.h>
#include <xgl/xgl_route.h>
#include <cstring>

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
        .seq_num = 0,
        .ack_num = 0,
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

TEST_F(XglNetworkTest, SendPacketNullPointer) {
    xgl_error_t err = xgl_network_send(nullptr, nullptr, false);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Receive Packet Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ReceivePacketForLocalNode) {
    /* Build a simple frame */
    uint8_t frame_buf[64];
    memset(frame_buf, 0, sizeof(frame_buf));
    
    /* Frame header */
    frame_buf[0] = XGL_SOF;                    // SOF
    frame_buf[1] = (1 << 4) | 1;               // Version 1, Data type 1
    frame_buf[2] = REMOTE_ID;                  // Source ID
    frame_buf[3] = LOCAL_ID;                   // Target ID (local)
    frame_buf[4] = 0x40;                       // Reliable TX
    frame_buf[5] = 0;                          // Attributes MSB
    frame_buf[6] = 5;                          // Data length LSB
    frame_buf[7] = 0;                          // Data length MSB
    frame_buf[8] = 0;                          // Seq num
    frame_buf[9] = 0;                          // Ack num
    frame_buf[10] = 0;                         // Reserved
    frame_buf[11] = 0;                         // CRC8 (not validated in this test)
    
    /* Payload */
    memcpy(&frame_buf[12], "hello", 5);
    
    /* CRC16 */
    frame_buf[17] = 0;
    frame_buf[18] = 0;
    
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
    
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf, 19);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_TRUE(upper_called);
    EXPECT_EQ(stats.rx_packets, 1);
}

TEST_F(XglNetworkTest, ReceivePacketForForwarding) {
    /* Add route for forwarding */
    xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1);
    
    /* Build frame for another node */
    uint8_t frame_buf[64];
    memset(frame_buf, 0, sizeof(frame_buf));
    
    frame_buf[0] = XGL_SOF;
    frame_buf[1] = (1 << 4) | 1;
    frame_buf[2] = REMOTE_ID;                  // Source
    frame_buf[3] = FORWARD_ID;                 // Target (not local)
    frame_buf[4] = 0x40;
    frame_buf[5] = 0;
    frame_buf[6] = 5;
    frame_buf[7] = 0;
    frame_buf[8] = 0;
    frame_buf[9] = 0;
    frame_buf[10] = XGL_DEFAULT_TTL;
    frame_buf[11] = 0;
    
    memcpy(&frame_buf[12], "hello", 5);
    frame_buf[17] = 0;
    frame_buf[18] = 0;
    
    int initial_tx_count = phy_tx_count;
    
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf, 19);
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

    uint8_t frame_buf[64];
    memset(frame_buf, 0, sizeof(frame_buf));

    frame_buf[0] = XGL_SOF;
    frame_buf[1] = (1 << 4) | 1;
    frame_buf[2] = REMOTE_ID;
    frame_buf[3] = 5;
    frame_buf[4] = 0x40;
    frame_buf[5] = 0;
    frame_buf[6] = 5;
    frame_buf[7] = 0;
    frame_buf[8] = 0;
    frame_buf[9] = 0;
    frame_buf[10] = XGL_DEFAULT_TTL;
    frame_buf[11] = 0;
    memcpy(&frame_buf[12], "hello", 5);
    frame_buf[17] = 0;
    frame_buf[18] = 0;

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf, 19), XGL_OK);
    EXPECT_EQ(first_phy_count, 0);
    EXPECT_EQ(second_phy_count, 1);
    EXPECT_EQ(stats.tx_packets, 1);
}

TEST_F(XglNetworkTest, ForwardingDropsExpiredTtl) {
    ASSERT_EQ(xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 100, 1), XGL_OK);

    uint8_t frame_buf[64];
    memset(frame_buf, 0, sizeof(frame_buf));

    frame_buf[0] = XGL_SOF;
    frame_buf[1] = (1 << 4) | 1;
    frame_buf[2] = REMOTE_ID;
    frame_buf[3] = FORWARD_ID;
    frame_buf[4] = 0x40;
    frame_buf[5] = 0;
    frame_buf[6] = 5;
    frame_buf[7] = 0;
    frame_buf[8] = 0;
    frame_buf[9] = 0;
    frame_buf[10] = 0;
    frame_buf[11] = 0;
    memcpy(&frame_buf[12], "hello", 5);
    frame_buf[17] = 0;
    frame_buf[18] = 0;

    EXPECT_EQ(xgl_network_receive(&network_ctx, nullptr, frame_buf, 19), XGL_ERR_TTL_EXPIRED);
    EXPECT_EQ(phy_tx_count, 0);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ReceivePacketNoRouteForForwarding) {
    /* Build frame for unknown node */
    uint8_t frame_buf[64];
    memset(frame_buf, 0, sizeof(frame_buf));
    
    frame_buf[0] = XGL_SOF;
    frame_buf[1] = (1 << 4) | 1;
    frame_buf[2] = REMOTE_ID;
    frame_buf[3] = 99;                         // Unknown target
    frame_buf[4] = 0x40;
    frame_buf[5] = 0;
    frame_buf[6] = 5;
    frame_buf[7] = 0;
    frame_buf[8] = 0;
    frame_buf[9] = 0;
    frame_buf[10] = XGL_DEFAULT_TTL;
    frame_buf[11] = 0;
    
    memcpy(&frame_buf[12], "hello", 5);
    frame_buf[17] = 0;
    frame_buf[18] = 0;
    
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf, 19);
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
    EXPECT_EQ(stats.rx_dropped, 1);
}

TEST_F(XglNetworkTest, ReceiveInvalidFrame) {
    uint8_t frame_buf[5] = {0};  // Too short
    
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr, frame_buf, 5);
    EXPECT_EQ(err, XGL_ERR_INVALID_FRAME);
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
