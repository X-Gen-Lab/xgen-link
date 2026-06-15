/**
 * \file            test_crc.cpp
 * \brief           CRC calculation unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_crc.h>
#include <cstring>

/*---------------------------------------------------------------------------*/
/* CRC8 MAXIM Tests                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test CRC8 with empty data
 */
TEST(XglCrc8Test, EmptyData) {
    uint8_t crc = xgl_crc8_maxim(nullptr, 0);
    EXPECT_EQ(crc, 0x00);
}

/**
 * \brief           Test CRC8 with single byte
 */
TEST(XglCrc8Test, SingleByte) {
    uint8_t data[] = {0x00};
    uint8_t crc = xgl_crc8_maxim(data, 1);
    EXPECT_EQ(crc, 0x00);
    
    data[0] = 0xFF;
    crc = xgl_crc8_maxim(data, 1);
    EXPECT_EQ(crc, 0x35);
}

/**
 * \brief           Test CRC8 with known test vectors
 */
TEST(XglCrc8Test, KnownVectors) {
    /* Test vector 1: "123456789" */
    const uint8_t data1[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint8_t crc1 = xgl_crc8_maxim(data1, sizeof(data1));
    EXPECT_EQ(crc1, 0xA1);  /* Known CRC8-MAXIM result */
    
    /* Test vector 2: Simple sequence */
    const uint8_t data2[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t crc2 = xgl_crc8_maxim(data2, sizeof(data2));
    EXPECT_NE(crc2, 0x00);  /* Should not be zero */
}

/**
 * \brief           Test CRC8 incremental calculation
 */
TEST(XglCrc8Test, IncrementalCalculation) {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    
    /* Calculate in one go */
    uint8_t crc_full = xgl_crc8_maxim(data, sizeof(data));
    
    /* Calculate incrementally */
    uint8_t crc_inc = 0x00;
    crc_inc = xgl_crc8_maxim_update(crc_inc, &data[0], 3);
    crc_inc = xgl_crc8_maxim_update(crc_inc, &data[3], 3);
    crc_inc = xgl_crc8_maxim_update(crc_inc, &data[6], 3);
    
    EXPECT_EQ(crc_full, crc_inc);
}

/**
 * \brief           Test CRC8 with null pointer handling
 */
TEST(XglCrc8Test, NullPointerHandling) {
    uint8_t crc = xgl_crc8_maxim_update(0x42, nullptr, 10);
    EXPECT_EQ(crc, 0x42);  /* Should return unchanged CRC */
}

/**
 * \brief           Test CRC8 with different data lengths
 */
TEST(XglCrc8Test, DifferentLengths) {
    uint8_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = (uint8_t)i;
    }
    
    /* Test various lengths */
    uint8_t crc1 = xgl_crc8_maxim(data, 1);
    uint8_t crc2 = xgl_crc8_maxim(data, 10);
    uint8_t crc3 = xgl_crc8_maxim(data, 100);
    uint8_t crc4 = xgl_crc8_maxim(data, 256);
    
    /* All should be different */
    EXPECT_NE(crc1, crc2);
    EXPECT_NE(crc2, crc3);
    EXPECT_NE(crc3, crc4);
}

/*---------------------------------------------------------------------------*/
/* CRC16 MODBUS Tests                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test CRC16 with empty data
 */
TEST(XglCrc16Test, EmptyData) {
    uint16_t crc = xgl_crc16_modbus(nullptr, 0);
    EXPECT_EQ(crc, 0xFFFF);
}

/**
 * \brief           Test CRC16 with single byte
 */
TEST(XglCrc16Test, SingleByte) {
    uint8_t data[] = {0x00};
    uint16_t crc = xgl_crc16_modbus(data, 1);
    EXPECT_EQ(crc, 0x40BF);
    
    data[0] = 0xFF;
    crc = xgl_crc16_modbus(data, 1);
    EXPECT_EQ(crc, 0x00FF);
}

/**
 * \brief           Test CRC16 with known test vectors
 */
TEST(XglCrc16Test, KnownVectors) {
    /* Test vector 1: "123456789" */
    const uint8_t data1[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t crc1 = xgl_crc16_modbus(data1, sizeof(data1));
    EXPECT_EQ(crc1, 0x4B37);  /* Known CRC16-MODBUS result */
    
    /* Test vector 2: Simple sequence */
    const uint8_t data2[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t crc2 = xgl_crc16_modbus(data2, sizeof(data2));
    EXPECT_NE(crc2, 0x0000);  /* Should not be zero */
}

/**
 * \brief           Test CRC16 incremental calculation
 */
TEST(XglCrc16Test, IncrementalCalculation) {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    
    /* Calculate in one go */
    uint16_t crc_full = xgl_crc16_modbus(data, sizeof(data));
    
    /* Calculate incrementally */
    uint16_t crc_inc = 0xFFFF;
    crc_inc = xgl_crc16_modbus_update(crc_inc, &data[0], 3);
    crc_inc = xgl_crc16_modbus_update(crc_inc, &data[3], 3);
    crc_inc = xgl_crc16_modbus_update(crc_inc, &data[6], 3);
    
    EXPECT_EQ(crc_full, crc_inc);
}

/**
 * \brief           Test CRC16 with null pointer handling
 */
TEST(XglCrc16Test, NullPointerHandling) {
    uint16_t crc = xgl_crc16_modbus_update(0x1234, nullptr, 10);
    EXPECT_EQ(crc, 0x1234);  /* Should return unchanged CRC */
}

/**
 * \brief           Test CRC16 with different data lengths
 */
TEST(XglCrc16Test, DifferentLengths) {
    uint8_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = (uint8_t)i;
    }
    
    /* Test various lengths */
    uint16_t crc1 = xgl_crc16_modbus(data, 1);
    uint16_t crc2 = xgl_crc16_modbus(data, 10);
    uint16_t crc3 = xgl_crc16_modbus(data, 100);
    uint16_t crc4 = xgl_crc16_modbus(data, 256);
    
    /* All should be different */
    EXPECT_NE(crc1, crc2);
    EXPECT_NE(crc2, crc3);
    EXPECT_NE(crc3, crc4);
}

/**
 * \brief           Test CRC16 with frame header simulation
 */
TEST(XglCrc16Test, FrameHeaderSimulation) {
    uint8_t header[24] = {
        0xA5, 0x5A,        /* binary magic */
        0x02,              /* version */
        0x18,              /* header_len */
        0x01,              /* packet_type */
        0x01,              /* flags */
        0x08,              /* ttl */
        0x00,              /* traffic_class */
        0x01, 0x00,        /* source_id */
        0x02, 0x00,        /* target_id */
        0x34, 0x12, 0x00, 0x00,  /* connection_id */
        0x78, 0x56, 0x34, 0x12,  /* packet_number */
        0x10, 0x00,        /* payload_len */
        0x00, 0x00         /* header_crc16 placeholder */
    };
    
    /* Simulate payload */
    uint8_t payload[16];
    for (int i = 0; i < 16; i++) {
        payload[i] = (uint8_t)i;
    }
    
    /* Calculate CRC16 for entire frame (header + payload) */
    uint16_t crc16 = 0xFFFF;
    crc16 = xgl_crc16_modbus_update(crc16, header, sizeof(header));
    crc16 = xgl_crc16_modbus_update(crc16, payload, 16);
    
    /* Verify CRC16 is calculated */
    EXPECT_NE(crc16, 0xFFFF);
}

/*---------------------------------------------------------------------------*/
/* Edge Case Tests                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test CRC with all zeros
 */
TEST(XglCrcEdgeTest, AllZeros) {
    uint8_t data[100];
    memset(data, 0x00, sizeof(data));
    
    uint8_t crc8 = xgl_crc8_maxim(data, sizeof(data));
    uint16_t crc16 = xgl_crc16_modbus(data, sizeof(data));
    
    EXPECT_EQ(crc8, 0x00);
    EXPECT_NE(crc16, 0xFFFF);  /* CRC16 should change from initial value */
}

/**
 * \brief           Test CRC with all ones
 */
TEST(XglCrcEdgeTest, AllOnes) {
    uint8_t data[100];
    memset(data, 0xFF, sizeof(data));
    
    uint8_t crc8 = xgl_crc8_maxim(data, sizeof(data));
    uint16_t crc16 = xgl_crc16_modbus(data, sizeof(data));
    
    EXPECT_NE(crc8, 0x00);
    EXPECT_NE(crc16, 0xFFFF);
}

/**
 * \brief           Test CRC with alternating pattern
 */
TEST(XglCrcEdgeTest, AlternatingPattern) {
    uint8_t data[100];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (i % 2) ? 0xFF : 0x00;
    }
    
    uint8_t crc8 = xgl_crc8_maxim(data, sizeof(data));
    uint16_t crc16 = xgl_crc16_modbus(data, sizeof(data));
    
    EXPECT_NE(crc8, 0x00);
    EXPECT_NE(crc16, 0xFFFF);
}
