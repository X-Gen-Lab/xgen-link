/**
 * \file            test_types.cpp
 * \brief           Unit tests for core data types and structures
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <cstring>
#include "xgl/xgl.h"
#include "xgl/xgl_types.h"
#include "xgl/xgl_wire.h"

/*---------------------------------------------------------------------------*/
/* Type Size Tests                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test frame header size
 */
TEST(XglTypesTest, FrameHeaderSize) {
    EXPECT_EQ(XGL_FRAME_HEADER_SIZE, XGL_WIRE_BASE_HEADER_SIZE);
    EXPECT_EQ(XGL_FRAME_HEADER_SIZE, 24);
}

/**
 * \brief           Test handle type
 */
TEST(XglTypesTest, HandleType) {
    /* Handle should be a pointer */
    EXPECT_EQ(sizeof(xgl_handle_t), sizeof(void*));
}

/*---------------------------------------------------------------------------*/
/* Configuration Structure Tests                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test configuration structure initialization
 */
TEST(XglTypesTest, ConfigStructure) {
    xgl_config_t config;
    std::memset(&config, 0, sizeof(config));
    
    /* Set basic fields */
    config.name = "test";
    config.source_id = 0x1234;
    config.memory.tx_pool_size = 1024;
    config.memory.rx_buffer_size = 256;
    config.protocol.ack_timeout_ms = 1000;
    config.protocol.max_retry_count = 5;
    config.protocol.window_size = 4;
    config.protocol.max_frame_size = 256;
    
    /* Verify fields */
    EXPECT_STREQ(config.name, "test");
    EXPECT_EQ(config.source_id, 0x1234);
    EXPECT_EQ(config.memory.tx_pool_size, 1024);
    EXPECT_EQ(config.memory.rx_buffer_size, 256);
    EXPECT_EQ(config.protocol.ack_timeout_ms, 1000);
    EXPECT_EQ(config.protocol.max_retry_count, 5);
    EXPECT_EQ(config.protocol.window_size, 4);
    EXPECT_EQ(config.protocol.max_frame_size, 256);
}

/**
 * \brief           Test configuration presets
 */
TEST(XglTypesTest, ConfigPresets) {
    /* Test tiny preset */
    xgl_config_t tiny = XGL_CONFIG_PRESET_TINY;
    EXPECT_EQ(tiny.memory.tx_pool_size, 1024);
    EXPECT_EQ(tiny.memory.rx_buffer_size, 160);  /* 12 header + 128 payload + 2 CRC + padding */
    EXPECT_EQ(tiny.protocol.window_size, 2);
    EXPECT_EQ(tiny.protocol.max_frame_size, 128);
    EXPECT_FALSE(tiny.features.enable_fragmentation);
    
    /* Test small preset */
    xgl_config_t small = XGL_CONFIG_PRESET_SMALL;
    EXPECT_EQ(small.memory.tx_pool_size, 2048);
    EXPECT_EQ(small.memory.rx_buffer_size, 288);  /* 12 header + 256 payload + 2 CRC + padding */
    EXPECT_EQ(small.protocol.window_size, 4);
    EXPECT_EQ(small.protocol.max_frame_size, 256);
    EXPECT_TRUE(small.features.enable_fragmentation);
    
    /* Test medium preset */
    xgl_config_t medium = XGL_CONFIG_PRESET_MEDIUM;
    EXPECT_EQ(medium.memory.tx_pool_size, 4096);
    EXPECT_EQ(medium.memory.rx_buffer_size, 544);  /* 12 header + 512 payload + 2 CRC + padding */
    EXPECT_EQ(medium.protocol.window_size, 8);
    EXPECT_EQ(medium.protocol.max_frame_size, 512);
    EXPECT_TRUE(medium.features.enable_fragmentation);
    EXPECT_FALSE(medium.features.enable_compression);
    
    /* Test large preset */
    xgl_config_t large = XGL_CONFIG_PRESET_LARGE;
    EXPECT_EQ(large.memory.tx_pool_size, 8192);
    EXPECT_EQ(large.memory.rx_buffer_size, 1056);  /* 12 header + 1024 payload + 2 CRC + padding */
    EXPECT_EQ(large.protocol.window_size, 16);
    EXPECT_EQ(large.protocol.max_frame_size, 1024);
    EXPECT_TRUE(large.features.enable_fragmentation);
    EXPECT_FALSE(large.features.enable_compression);
    EXPECT_FALSE(large.features.enable_encryption);
}

/*---------------------------------------------------------------------------*/
/* Statistics Structure Tests                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test statistics structure
 */
TEST(XglTypesTest, StatisticsStructure) {
    xgl_statistics_t stats;
    std::memset(&stats, 0, sizeof(stats));
    
    /* Set some values */
    stats.datalink.tx_packets = 100;
    stats.datalink.tx_bytes = 5000;
    stats.datalink.rx_packets = 95;
    stats.datalink.rx_bytes = 4800;
    stats.avg_rtt_ms = 50;
    
    /* Verify fields */
    EXPECT_EQ(stats.datalink.tx_packets, 100);
    EXPECT_EQ(stats.datalink.tx_bytes, 5000);
    EXPECT_EQ(stats.datalink.rx_packets, 95);
    EXPECT_EQ(stats.datalink.rx_bytes, 4800);
    EXPECT_EQ(stats.avg_rtt_ms, 50);
}

/*---------------------------------------------------------------------------*/
/* Transmission Data Structure Tests                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test standard transmission data structure
 */
TEST(XglTypesTest, TxDataStructure) {
    uint8_t data[] = {1, 2, 3, 4, 5};
    
    xgl_tx_data_t tx_data;
    tx_data.target_id = 0x1234;
    tx_data.data_type = 1;
    tx_data.data = data;
    tx_data.data_len = sizeof(data);
    tx_data.reliable = true;
    tx_data.priority = 3;
    
    /* Verify fields */
    EXPECT_EQ(tx_data.target_id, 0x1234);
    EXPECT_EQ(tx_data.data_type, 1);
    EXPECT_EQ(tx_data.data, data);
    EXPECT_EQ(tx_data.data_len, 5);
    EXPECT_TRUE(tx_data.reliable);
    EXPECT_EQ(tx_data.priority, 3);
}

/**
 * \brief           Test zero-copy transmission data structure
 */
TEST(XglTypesTest, TxDataZeroCopyStructure) {
    uint8_t buffer[256];
    
    xgl_tx_data_zerocopy_t tx_data;
    tx_data.buffer = buffer;
    tx_data.buffer_size = sizeof(buffer);
    tx_data.data_offset = XGL_FRAME_HEADER_SIZE;
    tx_data.data_len = 100;
    tx_data.target_id = 0x2345;
    tx_data.data_type = 2;
    tx_data.reliable = false;
    tx_data.priority = 5;
    
    /* Verify fields */
    EXPECT_EQ(tx_data.buffer, buffer);
    EXPECT_EQ(tx_data.buffer_size, 256);
    EXPECT_EQ(tx_data.data_offset, XGL_WIRE_BASE_HEADER_SIZE);
    EXPECT_EQ(tx_data.data_len, 100);
    EXPECT_EQ(tx_data.target_id, 0x2345);
    EXPECT_EQ(tx_data.data_type, 2);
    EXPECT_FALSE(tx_data.reliable);
    EXPECT_EQ(tx_data.priority, 5);
}

/*---------------------------------------------------------------------------*/
/* Attribute Bit Definitions Tests                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test attribute bit masks and shifts
 */
TEST(XglTypesTest, AttributeBitDefinitions) {
    /* Test reliable attribute */
    EXPECT_EQ(XGL_ATTR_RELIABLE_SHIFT, 6);
    EXPECT_EQ(XGL_ATTR_RELIABLE_MASK, 0xC0);
    EXPECT_EQ(XGL_ATTR_RELIABLE_NONE, 0x00);
    EXPECT_EQ(XGL_ATTR_RELIABLE_TX, 0x40);
    EXPECT_EQ(XGL_ATTR_RELIABLE_ACK, 0x80);
    
    /* Test fragment attribute */
    EXPECT_EQ(XGL_ATTR_FRAGMENT_SHIFT, 5);
    EXPECT_EQ(XGL_ATTR_FRAGMENT_MASK, 0x20);
    
    /* Test encrypt attribute */
    EXPECT_EQ(XGL_ATTR_ENCRYPT_SHIFT, 3);
    EXPECT_EQ(XGL_ATTR_ENCRYPT_MASK, 0x18);
    
    /* Test priority attribute */
    EXPECT_EQ(XGL_ATTR_PRIORITY_SHIFT, 0);
    EXPECT_EQ(XGL_ATTR_PRIORITY_MASK, 0x07);
    
    /* Test compress attribute */
    EXPECT_EQ(XGL_ATTR_COMPRESS_SHIFT, 6);
    EXPECT_EQ(XGL_ATTR_COMPRESS_MASK, 0xC0);
}

/**
 * \brief           Test attribute encoding
 */
TEST(XglTypesTest, AttributeEncoding) {
    uint8_t attr_lsb = 0;
    
    /* Set reliable TX */
    attr_lsb |= XGL_ATTR_RELIABLE_TX;
    EXPECT_EQ(attr_lsb & XGL_ATTR_RELIABLE_MASK, XGL_ATTR_RELIABLE_TX);
    
    /* Set fragment */
    attr_lsb |= XGL_ATTR_FRAGMENT_MASK;
    EXPECT_NE(attr_lsb & XGL_ATTR_FRAGMENT_MASK, 0);
    
    /* Set priority 5 */
    attr_lsb |= (5 << XGL_ATTR_PRIORITY_SHIFT);
    EXPECT_EQ(attr_lsb & XGL_ATTR_PRIORITY_MASK, 5);
}

/*---------------------------------------------------------------------------*/
/* Route Table Entry Tests                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test route table entry structure
 */
TEST(XglTypesTest, RouteTableEntry) {
    xgl_phy_ops_t phy_ops;
    
    xgl_route_item_t route;
    route.target_id = 0x3456;
    route.phy = &phy_ops;
    route.max_frame_size = 512;
    route.read_freq_hz = 1000;
    route.metric = 10;
    
    /* Verify fields */
    EXPECT_EQ(route.target_id, 0x3456);
    EXPECT_EQ(route.phy, &phy_ops);
    EXPECT_EQ(route.max_frame_size, 512);
    EXPECT_EQ(route.read_freq_hz, 1000);
    EXPECT_EQ(route.metric, 10);
}

/*---------------------------------------------------------------------------*/
/* Packet Data Structure Tests                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test packet data structure
 */
TEST(XglTypesTest, PacketDataStructure) {
    uint8_t data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    
    xgl_packet_data_t packet_data;
    packet_data.ref_count = 1;
    packet_data.data_len = sizeof(data);
    packet_data.data = data;
    
    /* Verify fields */
    EXPECT_EQ(packet_data.ref_count, 1);
    EXPECT_EQ(packet_data.data_len, 4);
    EXPECT_EQ(packet_data.data, data);
}

TEST(XglTypesTest, PacketUsesProductionAddressAndPacketNumberFields) {
    xgl_packet_t packet = {};

    packet.source_id = 0x4567;
    packet.target_id = 0x5678;
    packet.connection_id = 0x01020304;
    packet.packet_number = 0xA0B0C0D0;
    packet.session_epoch = 0x11223344;
    packet.packet_type = XGL_PACKET_TYPE_DATA;
    packet.flags = XGL_WIRE_FLAG_ACK_ELICITING;
    packet.ttl = 8;
    packet.traffic_class = 3;

    EXPECT_EQ(packet.source_id, 0x4567);
    EXPECT_EQ(packet.target_id, 0x5678);
    EXPECT_EQ(packet.connection_id, 0x01020304U);
    EXPECT_EQ(packet.packet_number, 0xA0B0C0D0U);
    EXPECT_EQ(packet.session_epoch, 0x11223344U);
    EXPECT_EQ(packet.packet_type, XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(packet.flags, XGL_WIRE_FLAG_ACK_ELICITING);
    EXPECT_EQ(packet.ttl, 8U);
    EXPECT_EQ(packet.traffic_class, 3U);
}

/*---------------------------------------------------------------------------*/
/* Constants Tests                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test protocol constants
 */
TEST(XglTypesTest, ProtocolConstants) {
    EXPECT_EQ(XGL_SOF, 0x55);
    EXPECT_EQ(XGL_CRC16_SIZE, 2);
    EXPECT_EQ(XGL_FRAME_HEADER_SIZE, XGL_WIRE_BASE_HEADER_SIZE);
}
