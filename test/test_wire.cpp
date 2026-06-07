#include <gtest/gtest.h>

#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_wire.h>

#include <cstring>

TEST(XglWireTest, EncodesProductionHeaderAtStableOffsets) {
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.flags = XGL_WIRE_FLAG_ACK_ELICITING | XGL_WIRE_FLAG_AUTHENTICATED;
    header.ttl = 9;
    header.traffic_class = 0x23;
    header.source_id = 0x1234;
    header.target_id = 0xABCD;
    header.connection_id = 0x01020304;
    header.packet_number = 0xA0B0C0D0;
    header.payload_len = 0x3456;

    uint8_t buffer[XGL_WIRE_BASE_HEADER_SIZE] = {};
    ASSERT_EQ(xgl_wire_encode_header(buffer, sizeof(buffer), &header), XGL_OK);

    EXPECT_EQ(buffer[0], 'X');
    EXPECT_EQ(buffer[1], 'G');
    EXPECT_EQ(buffer[2], XGL_WIRE_VERSION);
    EXPECT_EQ(buffer[3], XGL_WIRE_BASE_HEADER_SIZE);
    EXPECT_EQ(buffer[4], XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(buffer[5], static_cast<uint8_t>(XGL_WIRE_FLAG_ACK_ELICITING |
                                              XGL_WIRE_FLAG_AUTHENTICATED));
    EXPECT_EQ(buffer[6], 9U);
    EXPECT_EQ(buffer[7], 0x23U);
    EXPECT_EQ(xgl_deserialize_u16_le(&buffer[8]), 0x1234U);
    EXPECT_EQ(xgl_deserialize_u16_le(&buffer[10]), 0xABCDU);
    EXPECT_EQ(xgl_deserialize_u32_le(&buffer[12]), 0x01020304U);
    EXPECT_EQ(xgl_deserialize_u32_le(&buffer[16]), 0xA0B0C0D0U);
    EXPECT_EQ(xgl_deserialize_u16_le(&buffer[20]), 0x3456U);

    const uint16_t encoded_crc = xgl_deserialize_u16_le(&buffer[22]);
    uint8_t crc_input[XGL_WIRE_BASE_HEADER_SIZE] = {};
    memcpy(crc_input, buffer, sizeof(crc_input));
    crc_input[22] = 0;
    crc_input[23] = 0;
    EXPECT_EQ(encoded_crc, xgl_crc16_modbus(crc_input, sizeof(crc_input)));
}

TEST(XglWireTest, DecodesHeaderAndRejectsCorruptedCrc) {
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = XGL_PACKET_TYPE_ACK;
    header.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
    header.ttl = 1;
    header.source_id = 7;
    header.target_id = 8;
    header.connection_id = 0x99;
    header.packet_number = 42;
    header.payload_len = 0;

    uint8_t buffer[XGL_WIRE_BASE_HEADER_SIZE] = {};
    ASSERT_EQ(xgl_wire_encode_header(buffer, sizeof(buffer), &header), XGL_OK);

    xgl_wire_header_t decoded = {};
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, sizeof(buffer)), XGL_OK);
    EXPECT_EQ(decoded.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(decoded.source_id, 7U);
    EXPECT_EQ(decoded.target_id, 8U);
    EXPECT_EQ(decoded.connection_id, 0x99U);
    EXPECT_EQ(decoded.packet_number, 42U);

    buffer[4] ^= 0x01U;
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, sizeof(buffer)), XGL_ERR_CRC_FAILED);
}

TEST(XglWireTest, EncodesAndWalksTlvExtensions) {
    uint8_t buffer[32] = {};
    const uint8_t session_value[] = {
        0x78, 0x56, 0x34, 0x12,
        0x08, 0x07, 0x06, 0x05,
        0x04, 0x03, 0x02, 0x01
    };
    size_t written = 0;

    ASSERT_EQ(xgl_wire_encode_ext(buffer,
                                  sizeof(buffer),
                                  XGL_WIRE_EXT_SESSION,
                                  session_value,
                                  sizeof(session_value),
                                  &written),
              XGL_OK);
    EXPECT_EQ(written, sizeof(session_value) + XGL_WIRE_EXT_HEADER_SIZE);
    EXPECT_EQ(buffer[0], XGL_WIRE_EXT_SESSION);
    EXPECT_EQ(buffer[1], sizeof(session_value));

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor, buffer, written), XGL_OK);

    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_TRUE(ext.valid);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_SESSION);
    EXPECT_EQ(ext.len, sizeof(session_value));
    EXPECT_EQ(memcmp(ext.value, session_value, sizeof(session_value)), 0);

    EXPECT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_ERR_NOT_FOUND);
}

TEST(XglWireTest, EncodesAndDecodesAckRangeExtension) {
    const xgl_wire_ack_range_t ranges[] = {
        {.gap = 0, .length = 3},
        {.gap = 2, .length = 1}
    };
    uint8_t value[32] = {};
    size_t value_len = 0;

    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(value,
                                                  sizeof(value),
                                                  0x01020304U,
                                                  250U,
                                                  ranges,
                                                  2,
                                                  &value_len),
              XGL_OK);

    EXPECT_EQ(value_len, 17U);
    EXPECT_EQ(xgl_deserialize_u32_le(&value[0]), 0x01020304U);
    EXPECT_EQ(xgl_deserialize_u32_le(&value[4]), 250U);
    EXPECT_EQ(value[8], 2U);
    EXPECT_EQ(xgl_deserialize_u16_le(&value[9]), 0U);
    EXPECT_EQ(xgl_deserialize_u16_le(&value[11]), 3U);
    EXPECT_EQ(xgl_deserialize_u16_le(&value[13]), 2U);
    EXPECT_EQ(xgl_deserialize_u16_le(&value[15]), 1U);

    uint32_t largest_ack = 0;
    uint32_t ack_delay_us = 0;
    xgl_wire_ack_range_t decoded_ranges[2] = {};
    size_t decoded_count = 0;
    ASSERT_EQ(xgl_wire_decode_ack_range_ext_value(value,
                                                  value_len,
                                                  &largest_ack,
                                                  &ack_delay_us,
                                                  decoded_ranges,
                                                  2,
                                                  &decoded_count),
              XGL_OK);
    EXPECT_EQ(largest_ack, 0x01020304U);
    EXPECT_EQ(ack_delay_us, 250U);
    EXPECT_EQ(decoded_count, 2U);
    EXPECT_EQ(decoded_ranges[0].gap, 0U);
    EXPECT_EQ(decoded_ranges[0].length, 3U);
    EXPECT_EQ(decoded_ranges[1].gap, 2U);
    EXPECT_EQ(decoded_ranges[1].length, 1U);
}

TEST(XglWireTest, RejectsInvalidExtensionLength) {
    uint8_t invalid[] = {
        XGL_WIRE_EXT_ACK_RANGE,
        8,
        1, 2, 3
    };

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor, invalid, sizeof(invalid)), XGL_OK);

    xgl_wire_ext_t ext = {};
    EXPECT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_ERR_INVALID_FRAME);
}
