/**
 * \file            test_network.cpp
 * \brief           Network layer unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_network.h>
#include <xgl/xgl_route.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_packet_pool.h>

/*---------------------------------------------------------------------------*/
/* Test Helpers                                                              */
/*---------------------------------------------------------------------------*/

/* Simple PHY mock for testing */
static xgl_error_t test_phy_tx(const uint8_t* data, size_t len, void* user_data) {
    (void)data;
    (void)len;
    int* call_count = (int*)user_data;
    if (call_count) {
        (*call_count)++;
    }
    return XGL_OK;
}

static xgl_error_t test_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    (void)buffer;
    (void)len;
    (void)user_data;
    return XGL_OK;
}

/* Simple callback for testing */
static void test_rx_callback(xgl_handle_t handle, uint8_t source_id,
                             uint8_t data_type, const uint8_t* data,
                             size_t len, void* user_data) {
    (void)handle;
    (void)source_id;
    (void)data_type;
    (void)data;
    (void)len;
    int* call_count = (int*)user_data;
    if (call_count) {
        (*call_count)++;
    }
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
        xgl_network_init(&network_ctx, LOCAL_ID, &route_table,
                        nullptr, nullptr, nullptr, &stats);
        
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
    xgl_statistics_t stats;
    xgl_phy_ops_t phy_ops;
    int phy_tx_count;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, InitializeNetworkContext) {
    xgl_network_ctx_t ctx;
    xgl_route_table_t table;
    xgl_statistics_t test_stats;
    
    xgl_route_table_init(&table, 4, nullptr);
    
    xgl_error_t err = xgl_network_init(&ctx, 1, &table, nullptr, nullptr, nullptr, &test_stats);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(ctx.local_id, 1);
    EXPECT_EQ(ctx.route_table, &table);
    
    xgl_route_table_destroy(&table);
}

TEST_F(XglNetworkTest, InitializeWithNullContext) {
    xgl_route_table_t table;
    xgl_route_table_init(&table, 4, nullptr);
    
    xgl_error_t err = xgl_network_init(nullptr, 1, &table, nullptr, nullptr, nullptr, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
    
    xgl_route_table_destroy(&table);
}

TEST_F(XglNetworkTest, InitializeWithNullRouteTable) {
    xgl_network_ctx_t ctx;
    
    xgl_error_t err = xgl_network_init(&ctx, 1, nullptr, nullptr, nullptr, nullptr, nullptr);
    
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

/*---------------------------------------------------------------------------*/
/* Send Tests                                                                */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, SendPacketWithValidRoute) {
    /* Add route to route table */
    xgl_route_table_add(&route_table, REMOTE_ID, &phy_ops, 256, 1000, 100);
    
    /* Create packet */
    xgl_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.target_id = REMOTE_ID;
    packet.source_id = 0;  /* Will be set by network layer */
    
    /* Create packet data */
    uint8_t data[] = {0x01, 0x02, 0x03};
    xgl_packet_data_t packet_data;
    packet_data.ref_count = 1;
    packet_data.data_len = sizeof(data);
    packet_data.data = data;
    packet.data = &packet_data;
    
    /* Send packet */
    xgl_error_t err = xgl_network_send(&network_ctx, &packet, false);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(packet.source_id, LOCAL_ID);
    EXPECT_EQ(packet.version, XGL_PROTOCOL_VERSION);
    EXPECT_NE(packet.phy, nullptr);
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_EQ(stats.tx_bytes, sizeof(data));
}

TEST_F(XglNetworkTest, SendPacketWithNoRoute) {
    /* Create packet for target with no route */
    xgl_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.target_id = REMOTE_ID;
    
    /* Create packet data */
    uint8_t data[] = {0x01, 0x02, 0x03};
    xgl_packet_data_t packet_data;
    packet_data.ref_count = 1;
    packet_data.data_len = sizeof(data);
    packet_data.data = data;
    packet.data = &packet_data;
    
    /* Send packet */
    xgl_error_t err = xgl_network_send(&network_ctx, &packet, false);
    
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
    EXPECT_EQ(stats.tx_errors, 1);
}

TEST_F(XglNetworkTest, SendPacketWithNullPacket) {
    xgl_error_t err = xgl_network_send(&network_ctx, nullptr, false);
    
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Receive Tests                                                             */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ReceivePacketForLocalNode) {
    /* Create frame buffer */
    uint8_t frame_buf[128];
    xgl_frame_t frame;
    
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    
    /* Build frame */
    xgl_frame_build(&frame, REMOTE_ID, LOCAL_ID, 1, 0, 0,
                   payload, sizeof(payload), false, 0);
    
    /* Serialize frame */
    size_t bytes_written;
    xgl_frame_serialize(frame_buf, sizeof(frame_buf), &frame, &bytes_written);
    
    /* Setup callback */
    int rx_callback_count = 0;
    network_ctx.rx_callback = test_rx_callback;
    network_ctx.callback_user_data = &rx_callback_count;
    
    /* Receive packet */
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr,
                                         frame_buf, bytes_written);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.rx_packets, 1);
    EXPECT_EQ(stats.rx_bytes, sizeof(payload));
    EXPECT_EQ(rx_callback_count, 1);
}

TEST_F(XglNetworkTest, ReceivePacketForAnotherNode) {
    /* Add route for forwarding */
    xgl_route_table_add(&route_table, FORWARD_ID, &phy_ops, 256, 1000, 100);
    
    /* Create frame buffer */
    uint8_t frame_buf[128];
    xgl_frame_t frame;
    
    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    
    /* Build frame for another node */
    xgl_frame_build(&frame, REMOTE_ID, FORWARD_ID, 1, 0, 0,
                   payload, sizeof(payload), false, 0);
    
    /* Serialize frame */
    size_t bytes_written;
    xgl_frame_serialize(frame_buf, sizeof(frame_buf), &frame, &bytes_written);
    
    /* Reset PHY TX count */
    phy_tx_count = 0;
    
    /* Receive packet */
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr,
                                         frame_buf, bytes_written);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(stats.tx_packets, 1);
    EXPECT_EQ(stats.tx_bytes, sizeof(payload));
    EXPECT_EQ(phy_tx_count, 1);  /* PHY TX should be called for forwarding */
}

TEST_F(XglNetworkTest, ReceivePacketWithInvalidFrame) {
    /* Create invalid frame (too short) */
    uint8_t frame_buf[5] = {0x55, 0x01, 0x02, 0x03, 0x04};
    
    xgl_error_t err = xgl_network_receive(&network_ctx, nullptr,
                                         frame_buf, sizeof(frame_buf));
    
    EXPECT_EQ(err, XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(stats.rx_errors, 1);
}

/*---------------------------------------------------------------------------*/
/* Address Validation Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglNetworkTest, ValidateValidAddress) {
    bool valid = xgl_network_validate_address(&network_ctx, LOCAL_ID, REMOTE_ID);
    
    EXPECT_TRUE(valid);
}

TEST_F(XglNetworkTest, ValidateInvalidSourceBroadcast) {
    bool valid = xgl_network_validate_address(&network_ctx, LOCAL_ID, XGL_BROADCAST_ID);
    
    EXPECT_FALSE(valid);
}

TEST_F(XglNetworkTest, ValidateInvalidSourceZero) {
    bool valid = xgl_network_validate_address(&network_ctx, LOCAL_ID, 0);
    
    EXPECT_FALSE(valid);
}

TEST_F(XglNetworkTest, ValidateBroadcastTarget) {
    bool valid = xgl_network_validate_address(&network_ctx, XGL_BROADCAST_ID, REMOTE_ID);
    
    EXPECT_TRUE(valid);
}

/*---------------------------------------------------------------------------*/
/* Local Node Check Tests                                                    */
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
