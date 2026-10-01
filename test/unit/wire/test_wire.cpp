#include <wire/xgl_wire.h>

#include <cstring>
#include <gtest/gtest.h>
#include <vector>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

TEST(XglWireViewTest, AckRangesBorrowWireBytesWithoutAnOutputArray) {
    const uint8_t bytes[] = {42, 0, 0, 0, 250, 0, 0, 0, 2,
                             0,  0, 3, 0, 2,   0, 1, 0};
    xgl_wire_ack_range_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_ack_range_view(bytes, sizeof(bytes), &view),
              XGL_OK);
    EXPECT_EQ(view.largest_ack, 42U);
    EXPECT_EQ(view.ack_delay_us, 250U);
    EXPECT_EQ(view.range_count, 2U);
    EXPECT_EQ(view.ranges, bytes + 9U);
    xgl_wire_ack_range_t range = {};
    ASSERT_EQ(xgl_wire_ack_range_at(&view, 0U, &range), XGL_OK);
    EXPECT_EQ(range.gap, 0U);
    EXPECT_EQ(range.length, 3U);
    ASSERT_EQ(xgl_wire_ack_range_at(&view, 1U, &range), XGL_OK);
    EXPECT_EQ(range.gap, 2U);
    EXPECT_EQ(range.length, 1U);
    EXPECT_EQ(xgl_wire_ack_range_at(&view, 2U, &range), XGL_ERR_NOT_FOUND);
    EXPECT_EQ(xgl_wire_decode_ack_range_view(bytes, sizeof(bytes) - 1U, &view),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(view.ranges, nullptr);
}

TEST(XglWireViewTest, BorrowsValidatedBytesAndRejectsEveryTruncation) {
    uint8_t bytes[XGL_WIRE_BASE_HEADER_SIZE + 6] = {};
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE + 3;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = 1;
    header.target_id = 2;
    header.payload_len = 1;
    ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header), XGL_OK);
    bytes[XGL_WIRE_BASE_HEADER_SIZE] =
        250; /* Unknown noncritical TLVs are skipped. */
    bytes[XGL_WIRE_BASE_HEADER_SIZE + 1] = 1;
    bytes[XGL_WIRE_BASE_HEADER_SIZE + 2] = 99;
    bytes[header.header_len] = 42;
    xgb_serialize_u16_le(bytes + sizeof(bytes) - 2,
                         xgcrc_crc16_modbus(bytes, sizeof(bytes) - 2));
    xgl_wire_frame_view_t view = {};
    xgl_wire_decode_status_t status = XGL_WIRE_DECODE_INVALID;
    ASSERT_EQ(xgl_wire_decode_frame(&view, bytes, sizeof(bytes), &status),
              XGL_OK);
    EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
    EXPECT_EQ(view.frame_buf, bytes);
    EXPECT_EQ(view.payload, bytes + header.header_len);
    EXPECT_EQ(view.payload_len, 1U);
    EXPECT_EQ(view.payload[0], 42U);
    EXPECT_EQ(view.extensions_len, 3U);
    for (size_t size = 1; size < sizeof(bytes); ++size) {
        std::vector<uint8_t> truncated(bytes, bytes + size);
        EXPECT_NE(xgl_wire_decode_frame(&view, truncated.data(),
                                        truncated.size(), nullptr),
                  XGL_OK);
        EXPECT_EQ(view.frame_buf, nullptr);
        EXPECT_EQ(view.payload, nullptr);
    }
}

TEST(XglWireViewTest, RejectsForgedHeaderLengthBeforeReadingExtensions) {
    uint8_t bytes[XGL_WIRE_BASE_HEADER_SIZE + 2] = {};
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = UINT8_MAX;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = 1;
    header.target_id = 2;
    ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header), XGL_OK);
    xgl_wire_frame_view_t view = {};
    EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, sizeof(bytes), nullptr),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(view.frame_buf, nullptr);
}

TEST(XglWireViewTest, DistinguishesHeaderCrcAndFrameCrcFailures) {
    uint8_t bytes[XGL_WIRE_BASE_HEADER_SIZE + 3] = {};
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = 1;
    header.target_id = 2;
    header.payload_len = 1;
    ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header), XGL_OK);
    xgb_serialize_u16_le(bytes + sizeof(bytes) - 2,
                         xgcrc_crc16_modbus(bytes, sizeof(bytes) - 2));
    xgl_wire_frame_view_t view = {};
    xgl_wire_decode_status_t status;
    bytes[6] ^= 1;
    EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, sizeof(bytes), &status),
              XGL_ERR_CRC_FAILED);
    EXPECT_EQ(status, XGL_WIRE_DECODE_HEADER_CRC);
    bytes[6] ^= 1;
    bytes[XGL_WIRE_BASE_HEADER_SIZE] ^= 1;
    EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, sizeof(bytes), &status),
              XGL_ERR_CRC_FAILED);
    EXPECT_EQ(status, XGL_WIRE_DECODE_FRAME_CRC);
}

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

    EXPECT_EQ(buffer[0], XGL_WIRE_MAGIC_0);
    EXPECT_EQ(buffer[1], XGL_WIRE_MAGIC_1);
    EXPECT_EQ(buffer[2], XGL_WIRE_VERSION);
    EXPECT_EQ(buffer[3], XGL_WIRE_BASE_HEADER_SIZE);
    EXPECT_EQ(buffer[4], XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(buffer[5], static_cast<uint8_t>(XGL_WIRE_FLAG_ACK_ELICITING |
                                              XGL_WIRE_FLAG_AUTHENTICATED));
    EXPECT_EQ(buffer[6], 9U);
    EXPECT_EQ(buffer[7], 0x23U);
    EXPECT_EQ(xgb_deserialize_u16_le(&buffer[8]), 0x1234U);
    EXPECT_EQ(xgb_deserialize_u16_le(&buffer[10]), 0xABCDU);
    EXPECT_EQ(xgb_deserialize_u32_le(&buffer[12]), 0x01020304U);
    EXPECT_EQ(xgb_deserialize_u32_le(&buffer[16]), 0xA0B0C0D0U);
    EXPECT_EQ(xgb_deserialize_u16_le(&buffer[20]), 0x3456U);

    const uint16_t encoded_crc = xgb_deserialize_u16_le(&buffer[22]);
    uint8_t crc_input[XGL_WIRE_BASE_HEADER_SIZE] = {};
    memcpy(crc_input, buffer, sizeof(crc_input));
    crc_input[22] = 0;
    crc_input[23] = 0;
    EXPECT_EQ(encoded_crc, xgcrc_crc16_modbus(crc_input, sizeof(crc_input)));
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
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, sizeof(buffer)),
              XGL_ERR_CRC_FAILED);
}

TEST(XglWireTest, RejectsPacketTypeOutsideProtocolRange) {
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = static_cast<uint8_t>(XGL_PACKET_TYPE_CLOSE + 1U);
    header.ttl = 8;
    header.source_id = 7;
    header.target_id = 8;

    uint8_t buffer[XGL_WIRE_BASE_HEADER_SIZE] = {};
    EXPECT_EQ(xgl_wire_encode_header(buffer, sizeof(buffer), &header),
              XGL_ERR_INVALID_PARAM);

    header.packet_type = XGL_PACKET_TYPE_DATA;
    ASSERT_EQ(xgl_wire_encode_header(buffer, sizeof(buffer), &header), XGL_OK);
    buffer[4] = static_cast<uint8_t>(XGL_PACKET_TYPE_CLOSE + 1U);
    xgb_serialize_u16_le(&buffer[22], 0U);
    xgb_serialize_u16_le(&buffer[22],
                         xgcrc_crc16_modbus(buffer, sizeof(buffer)));

    xgl_wire_header_t decoded = {};
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, sizeof(buffer)),
              XGL_ERR_INVALID_FRAME);
}

TEST(XglWireTest, EncodesAndWalksTlvExtensions) {
    uint8_t buffer[32] = {};
    const uint8_t session_value[] = {0x78, 0x56, 0x34, 0x12, 0x08, 0x07,
                                     0x06, 0x05, 0x04, 0x03, 0x02, 0x01};
    size_t written = 0;

    ASSERT_EQ(xgl_wire_encode_ext(buffer, sizeof(buffer), XGL_WIRE_EXT_SESSION,
                                  session_value, sizeof(session_value),
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
    const xgl_wire_ack_range_t ranges[] = {{/* gap */ 0, /* length */ 3},
                                           {/* gap */ 2, /* length */ 1}};
    uint8_t value[32] = {};
    size_t value_len = 0;

    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(value, sizeof(value),
                                                  0x01020304U, 250U, ranges, 2,
                                                  &value_len),
              XGL_OK);

    EXPECT_EQ(value_len, 17U);
    EXPECT_EQ(xgb_deserialize_u32_le(&value[0]), 0x01020304U);
    EXPECT_EQ(xgb_deserialize_u32_le(&value[4]), 250U);
    EXPECT_EQ(value[8], 2U);
    EXPECT_EQ(xgb_deserialize_u16_le(&value[9]), 0U);
    EXPECT_EQ(xgb_deserialize_u16_le(&value[11]), 3U);
    EXPECT_EQ(xgb_deserialize_u16_le(&value[13]), 2U);
    EXPECT_EQ(xgb_deserialize_u16_le(&value[15]), 1U);

    uint32_t largest_ack = 0;
    uint32_t ack_delay_us = 0;
    xgl_wire_ack_range_t decoded_ranges[2] = {};
    size_t decoded_count = 0;
    ASSERT_EQ(xgl_wire_decode_ack_range_ext_value(
                  value, value_len, &largest_ack, &ack_delay_us, decoded_ranges,
                  2, &decoded_count),
              XGL_OK);
    EXPECT_EQ(largest_ack, 0x01020304U);
    EXPECT_EQ(ack_delay_us, 250U);
    EXPECT_EQ(decoded_count, 2U);
    EXPECT_EQ(decoded_ranges[0].gap, 0U);
    EXPECT_EQ(decoded_ranges[0].length, 3U);
    EXPECT_EQ(decoded_ranges[1].gap, 2U);
    EXPECT_EQ(decoded_ranges[1].length, 1U);
}

TEST(XglWireTest, EncodesAndDecodesSackExtension) {
    const uint8_t bitmap[] = {0b10101100U, 0b00010001U};
    uint8_t value[16] = {};
    size_t value_len = 0;

    ASSERT_EQ(xgl_wire_encode_sack_ext_value(value, sizeof(value), 0x0A0B0C0DU,
                                             bitmap, sizeof(bitmap),
                                             &value_len),
              XGL_OK);

    EXPECT_EQ(value_len, 7U);
    EXPECT_EQ(xgb_deserialize_u32_le(&value[0]), 0x0A0B0C0DU);
    EXPECT_EQ(value[4], sizeof(bitmap));
    EXPECT_EQ(memcmp(&value[5], bitmap, sizeof(bitmap)), 0);

    uint32_t base_packet = 0;
    uint8_t decoded_bitmap[2] = {};
    size_t decoded_bitmap_len = 0;
    ASSERT_EQ(xgl_wire_decode_sack_ext_value(
                  value, value_len, &base_packet, decoded_bitmap,
                  sizeof(decoded_bitmap), &decoded_bitmap_len),
              XGL_OK);
    EXPECT_EQ(base_packet, 0x0A0B0C0DU);
    EXPECT_EQ(decoded_bitmap_len, sizeof(bitmap));
    EXPECT_EQ(memcmp(decoded_bitmap, bitmap, sizeof(bitmap)), 0);
}

TEST(XglWireTest, EncodesAndDecodesFragmentExtension) {
    uint8_t value[16] = {};
    size_t value_len = 0;

    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(value, sizeof(value),
                                                 0x01020304U, 0x00001000U,
                                                 0x00004000U, &value_len),
              XGL_OK);

    EXPECT_EQ(value_len, 12U);
    EXPECT_EQ(xgb_deserialize_u32_le(&value[0]), 0x01020304U);
    EXPECT_EQ(xgb_deserialize_u32_le(&value[4]), 0x00001000U);
    EXPECT_EQ(xgb_deserialize_u32_le(&value[8]), 0x00004000U);

    uint32_t message_id = 0;
    uint32_t fragment_offset = 0;
    uint32_t message_len = 0;
    ASSERT_EQ(xgl_wire_decode_fragment_ext_value(value, value_len, &message_id,
                                                 &fragment_offset,
                                                 &message_len),
              XGL_OK);
    EXPECT_EQ(message_id, 0x01020304U);
    EXPECT_EQ(fragment_offset, 0x00001000U);
    EXPECT_EQ(message_len, 0x00004000U);
}

TEST(XglWireTest, EncodesAndDecodesSessionSecurityAndRouteExtensions) {
    uint8_t session[16] = {};
    size_t session_len = 0;
    ASSERT_EQ(
        xgl_wire_encode_session_ext_value(session, sizeof(session), 0x01020304U,
                                          0x1020304050607080ULL, &session_len),
        XGL_OK);
    EXPECT_EQ(session_len, 12U);
    EXPECT_EQ(xgb_deserialize_u32_le(&session[0]), 0x01020304U);
    EXPECT_EQ(session[4], 0x80U);
    EXPECT_EQ(session[11], 0x10U);

    uint32_t session_epoch = 0;
    uint64_t incarnation_id = 0;
    ASSERT_EQ(xgl_wire_decode_session_ext_value(
                  session, session_len, &session_epoch, &incarnation_id),
              XGL_OK);
    EXPECT_EQ(session_epoch, 0x01020304U);
    EXPECT_EQ(incarnation_id, 0x1020304050607080ULL);

    uint8_t security[16] = {};
    size_t security_len = 0;
    ASSERT_EQ(xgl_wire_encode_security_ext_value(
                  security, sizeof(security), 0xA0B0C0D0U,
                  0x0102030405060708ULL, 16, &security_len),
              XGL_OK);
    EXPECT_EQ(security_len, 13U);
    EXPECT_EQ(xgb_deserialize_u32_le(&security[0]), 0xA0B0C0D0U);
    EXPECT_EQ(security[12], 16U);

    uint32_t key_id = 0;
    uint64_t nonce_id = 0;
    uint8_t tag_len = 0;
    ASSERT_EQ(xgl_wire_decode_security_ext_value(security, security_len,
                                                 &key_id, &nonce_id, &tag_len),
              XGL_OK);
    EXPECT_EQ(key_id, 0xA0B0C0D0U);
    EXPECT_EQ(nonce_id, 0x0102030405060708ULL);
    EXPECT_EQ(tag_len, 16U);

    uint8_t route[16] = {};
    size_t route_len = 0;
    ASSERT_EQ(xgl_wire_encode_route_ext_value(route, sizeof(route), 0x1234,
                                              0x5678, 0xCAFEBABEU, 9,
                                              &route_len),
              XGL_OK);
    EXPECT_EQ(route_len, 10U);
    EXPECT_EQ(xgb_deserialize_u16_le(&route[0]), 0x1234U);
    EXPECT_EQ(xgb_deserialize_u16_le(&route[2]), 0x5678U);
    EXPECT_EQ(xgb_deserialize_u32_le(&route[4]), 0xCAFEBABEU);
    EXPECT_EQ(xgb_deserialize_u16_le(&route[8]), 9U);

    uint16_t previous_hop = 0;
    uint16_t next_hop = 0;
    uint32_t route_epoch = 0;
    uint16_t metric = 0;
    ASSERT_EQ(xgl_wire_decode_route_ext_value(route, route_len, &previous_hop,
                                              &next_hop, &route_epoch, &metric),
              XGL_OK);
    EXPECT_EQ(previous_hop, 0x1234U);
    EXPECT_EQ(next_hop, 0x5678U);
    EXPECT_EQ(route_epoch, 0xCAFEBABEU);
    EXPECT_EQ(metric, 9U);
}

TEST(XglWireTest, RejectsZeroLengthSecurityTag) {
    uint8_t security[16] = {};
    size_t security_len = 0;

    EXPECT_EQ(xgl_wire_encode_security_ext_value(
                  security, sizeof(security), 0xA0B0C0D0U,
                  0x0102030405060708ULL, 0, &security_len),
              XGL_ERR_INVALID_PARAM);

    const uint8_t encoded_zero_tag[] = {0xD0, 0xC0, 0xB0, 0xA0, 0x08,
                                        0x07, 0x06, 0x05, 0x04, 0x03,
                                        0x02, 0x01, 0x00};
    uint32_t key_id = 0;
    uint64_t nonce_id = 0;
    uint8_t tag_len = 0;
    EXPECT_EQ(xgl_wire_decode_security_ext_value(encoded_zero_tag,
                                                 sizeof(encoded_zero_tag),
                                                 &key_id, &nonce_id, &tag_len),
              XGL_ERR_INVALID_FRAME);
}

TEST(XglWireTest, RejectsV2WithoutAutomaticDowngrade) {
    uint8_t bytes[XGL_WIRE_BASE_HEADER_SIZE] = {};
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = 1U;
    header.target_id = 2U;
    ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header), XGL_OK);
    bytes[2] = 2U;
    bytes[22] = bytes[23] = 0U;
    xgb_serialize_u16_le(bytes + 22, xgcrc_crc16_modbus(bytes, sizeof(bytes)));
    EXPECT_EQ(xgl_wire_decode_header(&header, bytes, sizeof(bytes)),
              XGL_ERR_INVALID_FRAME);
}

TEST(XglWireTest, RejectsInvalidExtensionLength) {
    uint8_t invalid[] = {XGL_WIRE_EXT_ACK_RANGE, 8, 1, 2, 3};

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor, invalid, sizeof(invalid)),
              XGL_OK);

    xgl_wire_ext_t ext = {};
    EXPECT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_ERR_INVALID_FRAME);
}

TEST(XglWireTest, RejectsUnimplementedEncryptionFlagsAndTrafficClasses) {
    const uint8_t unsupported_flags[] = {XGL_WIRE_FLAG_ENCRYPTED, 0U, 0U, 0U};
    const uint8_t unsupported_classes[] = {0U, XGL_TRAFFIC_ENCRYPTION_AES128,
                                           XGL_TRAFFIC_ENCRYPTION_CHACHA20,
                                           XGL_TRAFFIC_ENCRYPTION_MASK};
    for (size_t i = 0; i < 4U; ++i) {
        xgl_wire_header_t header = {};
        header.version = XGL_WIRE_VERSION;
        header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
        header.packet_type = XGL_PACKET_TYPE_DATA;
        header.source_id = 1U;
        header.target_id = 2U;
        uint8_t bytes[XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE] = {};
        ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header),
                  XGL_OK);
        header.flags = unsupported_flags[i];
        header.traffic_class = unsupported_classes[i];
        uint8_t rejected_output[XGL_WIRE_BASE_HEADER_SIZE] = {};
        EXPECT_EQ(xgl_wire_encode_header(rejected_output,
                                         sizeof(rejected_output), &header),
                  XGL_ERR_INVALID_PARAM);
        bytes[5] = unsupported_flags[i];
        bytes[7] = unsupported_classes[i];
        bytes[22] = bytes[23] = 0U;
        xgb_serialize_u16_le(
            bytes + 22U, xgcrc_crc16_modbus(bytes, XGL_WIRE_BASE_HEADER_SIZE));
        xgb_serialize_u16_le(
            bytes + XGL_WIRE_BASE_HEADER_SIZE,
            xgcrc_crc16_modbus(bytes, XGL_WIRE_BASE_HEADER_SIZE));
        EXPECT_EQ(xgl_wire_decode_header(&header, bytes, sizeof(bytes)),
                  XGL_ERR_INVALID_FRAME);
        xgl_wire_frame_view_t view = {};
        EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, sizeof(bytes), nullptr),
                  XGL_ERR_INVALID_FRAME);
    }
}
