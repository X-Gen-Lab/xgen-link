/**
 * \file            test_wire.cpp
 * \brief           Exercise the Wire v1 encoding and decoding contract
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-06-07
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <wire/xgl_wire.h>

#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>
#include <wire/xgl_crc16.h>
#include <wire/xgl_wire_bytes.h>

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
    header.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
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
    xgl_serialize_u16_le(bytes + sizeof(bytes) - 2,
                         xgl_crc16_modbus(bytes, sizeof(bytes) - 2));
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
    header.flags = XGL_WIRE_FLAG_HAS_EXTENSIONS;
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
    xgl_serialize_u16_le(bytes + sizeof(bytes) - 2,
                         xgl_crc16_modbus(bytes, sizeof(bytes) - 2));
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
    header.flags = XGL_WIRE_FLAG_ACK_ELICITING;
    header.ttl = 9;
    header.traffic_class = 0x43;
    header.source_id = 0x1234;
    header.target_id = 0xABCD;
    header.connection_id = 0x01020304;
    header.packet_number = 0xA0B0C0D0;
    header.payload_len = 0x3456;

    uint8_t buffer[XGL_WIRE_BASE_HEADER_SIZE] = {};
    ASSERT_EQ(xgl_wire_encode_header(buffer, sizeof(buffer), &header), XGL_OK);

    EXPECT_EQ(buffer[0], XGL_WIRE_MAGIC_0);
    EXPECT_EQ(buffer[1], XGL_WIRE_MAGIC_1);
    EXPECT_EQ(buffer[2], 1U);
    EXPECT_EQ(buffer[3], XGL_WIRE_BASE_HEADER_SIZE);
    EXPECT_EQ(buffer[4], XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(buffer[5], 0x01U);
    EXPECT_EQ(buffer[6], 9U);
    EXPECT_EQ(buffer[7], 0x43U);
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

TEST(XglWireTest, ModbusMatchesTheIndependentCheckValue) {
    constexpr uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(xgl_crc16_modbus(data, sizeof(data)), 0x4B37U);
    const uint16_t prefix = xgl_crc16_modbus_update(UINT16_MAX, data, 4U);
    EXPECT_EQ(xgl_crc16_modbus_update(prefix, data + 4U, 5U), 0x4B37U);
}

TEST(XglWireTest, MatchCompleteDataFrameAgainstIndependentGoldenBytes) {
    constexpr std::array<uint8_t, 45> golden = {
        0xA5U, 0x5AU, 0x01U, 0x29U, 0x01U, 0x03U, 0x09U, 0x43U, 0x34U,
        0x12U, 0xCDU, 0xABU, 0x04U, 0x03U, 0x02U, 0x01U, 0x11U, 0x00U,
        0x00U, 0x00U, 0x02U, 0x00U, 0xE8U, 0x9EU, 0x05U, 0x01U, 0x07U,
        0x01U, 0x0CU, 0x0DU, 0x0CU, 0x0BU, 0x0AU, 0x80U, 0x70U, 0x60U,
        0x50U, 0x40U, 0x30U, 0x20U, 0x10U, 0x42U, 0x87U, 0xEAU, 0xA7U};
    xgl_wire_frame_view_t decoded = {};
    ASSERT_EQ(
        xgl_wire_decode_frame(&decoded, golden.data(), golden.size(), nullptr),
        XGL_OK);
    EXPECT_EQ(decoded.header.version, 1U);
    EXPECT_EQ(decoded.header.source_id, 0x1234U);
    EXPECT_EQ(decoded.header.target_id, 0xABCDU);
    EXPECT_EQ(decoded.header.connection_id, 0x01020304U);
    EXPECT_EQ(decoded.header.packet_number, 17U);
    EXPECT_EQ(decoded.header.header_crc16, 0x9EE8U);
    EXPECT_EQ(decoded.data_type, 7U);
    EXPECT_EQ(decoded.session_epoch, 0x0A0B0C0DU);
    ASSERT_EQ(decoded.payload_len, 2U);
    EXPECT_EQ(decoded.payload[0], 0x42U);
    EXPECT_EQ(decoded.payload[1], 0x87U);

    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = 41U;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.flags = XGL_WIRE_FLAG_ACK_ELICITING | XGL_WIRE_FLAG_HAS_EXTENSIONS;
    header.ttl = 9U;
    header.traffic_class = XGL_RELIABILITY_ACK_ELICITING | 3U;
    header.source_id = 0x1234U;
    header.target_id = 0xABCDU;
    header.connection_id = 0x01020304U;
    header.packet_number = 17U;
    header.payload_len = 2U;
    std::array<uint8_t, golden.size()> encoded = {};
    ASSERT_EQ(xgl_wire_encode_header(encoded.data(), encoded.size(), &header),
              XGL_OK);
    size_t type_size = 0;
    const uint8_t data_type = 7U;
    ASSERT_EQ(xgl_wire_encode_ext(encoded.data() + 24U, encoded.size() - 24U,
                                  XGL_WIRE_EXT_DATA_TYPE, &data_type,
                                  sizeof(data_type), &type_size),
              XGL_OK);
    ASSERT_EQ(type_size, 3U);
    uint8_t session[12] = {};
    size_t value_size = 0;
    ASSERT_EQ(
        xgl_wire_encode_session_ext_value(session, sizeof(session), 0x0A0B0C0DU,
                                          0x1020304050607080ULL, &value_size),
        XGL_OK);
    size_t session_size = 0;
    ASSERT_EQ(xgl_wire_encode_ext(encoded.data() + 27U, encoded.size() - 27U,
                                  XGL_WIRE_EXT_SESSION, session, value_size,
                                  &session_size),
              XGL_OK);
    ASSERT_EQ(session_size, 14U);
    encoded[41] = 0x42U;
    encoded[42] = 0x87U;
    xgl_serialize_u16_le(encoded.data() + 43U,
                         xgl_crc16_modbus(encoded.data(), 43U));
    EXPECT_EQ(encoded, golden);
}

TEST(XglWireTest, DecodesHeaderAndRejectsCorruptedCrc) {
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = XGL_PACKET_TYPE_ACK;
    header.traffic_class = XGL_RELIABILITY_ACK_ONLY;
    header.ttl = 1;
    header.source_id = 7;
    header.target_id = 8;
    header.connection_id = 0x99;
    header.payload_len = 0;

    uint8_t buffer[XGL_WIRE_BASE_HEADER_SIZE] = {};
    ASSERT_EQ(xgl_wire_encode_header(buffer, sizeof(buffer), &header), XGL_OK);

    xgl_wire_header_t decoded = {};
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, sizeof(buffer)), XGL_OK);
    EXPECT_EQ(decoded.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(decoded.source_id, 7U);
    EXPECT_EQ(decoded.target_id, 8U);
    EXPECT_EQ(decoded.connection_id, 0x99U);
    EXPECT_EQ(decoded.packet_number, 0U);

    buffer[4] ^= 0x01U;
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, sizeof(buffer)),
              XGL_ERR_CRC_FAILED);
}

TEST(XglWireTest, RejectsPacketTypeOutsideProtocolRange) {
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = 4U;
    header.ttl = 8;
    header.source_id = 7;
    header.target_id = 8;

    uint8_t buffer[XGL_WIRE_BASE_HEADER_SIZE] = {};
    EXPECT_EQ(xgl_wire_encode_header(buffer, sizeof(buffer), &header),
              XGL_ERR_INVALID_PARAM);

    header.packet_type = XGL_PACKET_TYPE_DATA;
    ASSERT_EQ(xgl_wire_encode_header(buffer, sizeof(buffer), &header), XGL_OK);
    buffer[4] = 4U;
    xgl_serialize_u16_le(&buffer[22], 0U);
    xgl_serialize_u16_le(&buffer[22], xgl_crc16_modbus(buffer, sizeof(buffer)));

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
    EXPECT_EQ(xgl_deserialize_u32_le(&value[0]), 0x01020304U);
    EXPECT_EQ(xgl_deserialize_u32_le(&value[4]), 250U);
    EXPECT_EQ(value[8], 2U);
    EXPECT_EQ(xgl_deserialize_u16_le(&value[9]), 0U);
    EXPECT_EQ(xgl_deserialize_u16_le(&value[11]), 3U);
    EXPECT_EQ(xgl_deserialize_u16_le(&value[13]), 2U);
    EXPECT_EQ(xgl_deserialize_u16_le(&value[15]), 1U);

    xgl_wire_ack_range_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_ack_range_view(value, value_len, &view), XGL_OK);
    EXPECT_EQ(view.largest_ack, 0x01020304U);
    EXPECT_EQ(view.ack_delay_us, 250U);
    ASSERT_EQ(view.range_count, 2U);
    for (size_t index = 0; index < view.range_count; ++index) {
        xgl_wire_ack_range_t decoded = {};
        ASSERT_EQ(xgl_wire_ack_range_at(&view, index, &decoded), XGL_OK);
        EXPECT_EQ(decoded.gap, ranges[index].gap);
        EXPECT_EQ(decoded.length, ranges[index].length);
    }
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
    EXPECT_EQ(xgl_deserialize_u32_le(&value[0]), 0x0A0B0C0DU);
    EXPECT_EQ(value[4], sizeof(bitmap));
    EXPECT_EQ(memcmp(&value[5], bitmap, sizeof(bitmap)), 0);

    xgl_wire_sack_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_sack_view(value, value_len, &view), XGL_OK);
    EXPECT_EQ(view.base_packet, 0x0A0B0C0DU);
    ASSERT_EQ(view.bitmap_len, sizeof(bitmap));
    EXPECT_EQ(view.bitmap, value + 5U);
    EXPECT_EQ(memcmp(view.bitmap, bitmap, sizeof(bitmap)), 0);
}

TEST(XglWireTest, EncodesAndDecodesSessionAndSecurityExtensions) {
    uint8_t session[16] = {};
    size_t session_len = 0;
    ASSERT_EQ(
        xgl_wire_encode_session_ext_value(session, sizeof(session), 0x01020304U,
                                          0x1020304050607080ULL, &session_len),
        XGL_OK);
    EXPECT_EQ(session_len, 12U);
    EXPECT_EQ(xgl_deserialize_u32_le(&session[0]), 0x01020304U);
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
    EXPECT_EQ(xgl_deserialize_u32_le(&security[0]), 0xA0B0C0D0U);
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

TEST(XglWireTest, RejectsNonV1WithoutAutomaticDowngrade) {
    uint8_t bytes[XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE] = {};
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = 1U;
    header.target_id = 2U;
    ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header), XGL_OK);
    const uint8_t retired_versions[] = {0U, 2U, 3U, 0xFFU};
    for (const uint8_t version : retired_versions) {
        SCOPED_TRACE(static_cast<unsigned>(version));
        bytes[2] = version;
        bytes[22] = bytes[23] = 0U;
        xgl_serialize_u16_le(
            bytes + 22, xgl_crc16_modbus(bytes, XGL_WIRE_BASE_HEADER_SIZE));
        xgl_serialize_u16_le(
            bytes + XGL_WIRE_BASE_HEADER_SIZE,
            xgl_crc16_modbus(bytes, XGL_WIRE_BASE_HEADER_SIZE));
        EXPECT_EQ(xgl_wire_decode_header(&header, bytes, sizeof(bytes)),
                  XGL_ERR_INVALID_FRAME);
        xgl_wire_frame_view_t view = {};
        xgl_wire_decode_status_t status = XGL_WIRE_DECODE_OK;
        EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, sizeof(bytes), &status),
                  XGL_ERR_INVALID_FRAME);
        EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
        EXPECT_EQ(view.frame_buf, nullptr);
    }
}

TEST(XglWireTest, EnforcesCanonicalDataAckAndControlFields) {
    struct HeaderFields {
        uint8_t type;
        uint8_t flags;
        uint8_t traffic_class;
        uint32_t packet_number;
        uint16_t payload_len;
        bool valid;
    };

    constexpr HeaderFields cases[] = {
        {1U, 0U, 7U, 0U, 0U, true},     {1U, 1U, 0x47U, 17U, 1U, true},
        {2U, 0U, 0x87U, 0U, 0U, true},  {3U, 8U, 7U, 0U, 0U, true},
        {1U, 0U, 7U, 1U, 0U, false},    {1U, 0U, 0x47U, 0U, 0U, false},
        {1U, 1U, 7U, 0U, 0U, false},    {1U, 0U, 0x87U, 0U, 0U, false},
        {1U, 8U, 7U, 0U, 0U, false},    {1U, 2U, 7U, 0U, 0U, false},
        {2U, 1U, 0x87U, 0U, 0U, false}, {2U, 8U, 0x87U, 0U, 0U, false},
        {2U, 0U, 7U, 0U, 0U, false},    {2U, 0U, 0x87U, 1U, 0U, false},
        {2U, 0U, 0x87U, 0U, 1U, false}, {3U, 0U, 7U, 0U, 0U, false},
        {3U, 9U, 0x47U, 0U, 0U, false}, {3U, 8U, 0x87U, 0U, 0U, false},
        {3U, 8U, 7U, 1U, 0U, false},    {3U, 8U, 7U, 0U, 1U, false},
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(&entry - cases);
        uint8_t bytes[XGL_WIRE_BASE_HEADER_SIZE] = {};
        xgl_wire_header_t header = {};
        header.version = XGL_WIRE_VERSION;
        header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
        header.packet_type = XGL_PACKET_TYPE_DATA;
        header.source_id = 1U;
        header.target_id = 2U;
        ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header),
                  XGL_OK);
        header.packet_type = entry.type;
        header.flags = entry.flags;
        header.traffic_class = entry.traffic_class;
        header.packet_number = entry.packet_number;
        header.payload_len = entry.payload_len;
        EXPECT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header),
                  entry.valid ? XGL_OK : XGL_ERR_INVALID_PARAM);

        bytes[4] = entry.type;
        bytes[5] = entry.flags;
        bytes[7] = entry.traffic_class;
        xgl_serialize_u32_le(bytes + 16U, entry.packet_number);
        xgl_serialize_u16_le(bytes + 20U, entry.payload_len);
        xgl_serialize_u16_le(bytes + 22U, 0U);
        xgl_serialize_u16_le(bytes + 22U,
                             xgl_crc16_modbus(bytes, sizeof(bytes)));
        EXPECT_EQ(xgl_wire_decode_header(&header, bytes, sizeof(bytes)),
                  entry.valid ? XGL_OK : XGL_ERR_INVALID_FRAME);
    }
}

TEST(XglWireTest, RejectsInvalidExtensionLength) {
    uint8_t invalid[] = {XGL_WIRE_EXT_ACK_RANGE, 8, 1, 2, 3};

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor, invalid, sizeof(invalid)),
              XGL_OK);

    xgl_wire_ext_t ext = {};
    EXPECT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_ERR_INVALID_FRAME);
}

TEST(XglWireTest, RejectsUnsupportedFlagsAndTrafficClasses) {
    const uint8_t unsupported_flags[] = {0x10U, 0x20U, 0x40U, 0x80U,
                                         0U,    0U,    0U,    0U};
    const uint8_t unsupported_classes[] = {0U,    0U,    0U,    0U,
                                           0x08U, 0x10U, 0x20U, 0xC0U};
    for (size_t i = 0; i < sizeof(unsupported_flags); ++i) {
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
        xgl_serialize_u16_le(
            bytes + 22U, xgl_crc16_modbus(bytes, XGL_WIRE_BASE_HEADER_SIZE));
        xgl_serialize_u16_le(
            bytes + XGL_WIRE_BASE_HEADER_SIZE,
            xgl_crc16_modbus(bytes, XGL_WIRE_BASE_HEADER_SIZE));
        EXPECT_EQ(xgl_wire_decode_header(&header, bytes, sizeof(bytes)),
                  XGL_ERR_INVALID_FRAME);
        xgl_wire_frame_view_t view = {};
        EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, sizeof(bytes), nullptr),
                  XGL_ERR_INVALID_FRAME);
    }
}
