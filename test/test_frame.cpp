/**
 * \file            test_frame.cpp
 * \brief           Frame encapsulation unit tests
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl.h>

#include <cstring>
#include <gtest/gtest.h>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

#include "test_security_helpers.h"

static xgl_error_t frame_test_auth_sign(const xgl_auth_input_t* input,
                                        uint8_t* tag, size_t tag_capacity,
                                        size_t* tag_len, void* user_data) {
    const uint32_t key_id = input->key_id;
    const uint8_t* aad = input->aad;
    const size_t aad_len = input->aad_len;
    const uint8_t* payload = input->payload;
    const size_t payload_len = input->payload_len;
    if (tag == nullptr || tag_len == nullptr || tag_capacity < 4U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if (user_data != nullptr) {
        size_t* sign_count = static_cast<size_t*>(user_data);
        (*sign_count)++;
    }

    uint32_t acc = key_id;
    for (size_t i = 0; i < aad_len; ++i) {
        acc = (acc * 33U) ^ aad[i];
    }
    for (size_t i = 0; i < payload_len; ++i) {
        acc = (acc * 33U) ^ payload[i];
    }

    xgb_serialize_u32_le(tag, acc);
    *tag_len = 4U;
    return XGL_OK;
}

static xgl_error_t frame_test_auth_verify(const xgl_auth_input_t* input,
                                          const uint8_t* tag, size_t tag_len,
                                          bool* valid, void* user_data) {
    (void)user_data;
    uint8_t expected[4] = {};
    size_t expected_len = 0;
    xgl_error_t err = frame_test_auth_sign(input, expected, sizeof(expected),
                                           &expected_len, nullptr);
    if (err != XGL_OK) {
        return err;
    }

    *valid = tag_len == expected_len &&
             std::memcmp(tag, expected, expected_len) == 0;
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Frame Building Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, BuildBasicFrame) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};

    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.connection_id = 0x01020304;
    params.packet_number = 0x11223344;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.priority = 3;

    xgl_error_t result = xgl_frame_build(&frame, &params);

    EXPECT_EQ(result, XGL_OK);
    EXPECT_EQ(frame.header.version, XGL_WIRE_VERSION);
    EXPECT_EQ(frame.header.header_len, XGL_WIRE_BASE_HEADER_SIZE);
    EXPECT_EQ(frame.header.packet_type, XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(frame.header.source_id, 0x10);
    EXPECT_EQ(frame.header.target_id, 0x20);
    EXPECT_EQ(frame.header.connection_id, 0x01020304U);
    EXPECT_EQ(frame.header.packet_number, 0x11223344U);
    EXPECT_EQ(frame.header.payload_len, sizeof(payload));
    EXPECT_EQ(frame.payload, payload);
    EXPECT_EQ(frame.payload_len, sizeof(payload));

    EXPECT_NE(frame.header.flags & XGL_WIRE_FLAG_ACK_ELICITING, 0);
    EXPECT_EQ(frame.header.traffic_class & XGL_TRAFFIC_PRIORITY_MASK, 3);
}

TEST(XglFrameTest, AckOnlyFrameDoesNotSetControlFlag) {
    xgl_frame_t frame;

    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.packet_type = XGL_PACKET_TYPE_ACK;
    params.connection_id = 0x01020304;
    params.packet_number = 0x11223344;
    params.payload = nullptr;
    params.payload_len = 0;
    params.reliable = false;
    params.reliability_class = XGL_RELIABILITY_ACK_ONLY;
    params.priority = 7;

    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    EXPECT_EQ(frame.header.packet_type, XGL_PACKET_TYPE_ACK);
    EXPECT_EQ(frame.header.traffic_class & XGL_RELIABILITY_CLASS_MASK,
              XGL_RELIABILITY_ACK_ONLY);
    EXPECT_EQ(frame.header.flags & XGL_WIRE_FLAG_CONTROL, 0U);
}

TEST(XglFrameTest, BuildFrameNullPointer) {
    const uint8_t payload[] = {0x01, 0x02};

    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = 0x05;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;

    xgl_error_t result = xgl_frame_build(nullptr, &params);
    EXPECT_EQ(result, XGL_ERR_NULL_POINTER);

    xgl_frame_t frame;
    result = xgl_frame_build(&frame, nullptr);
    EXPECT_EQ(result, XGL_ERR_NULL_POINTER);
}

TEST(XglFrameTest, BuildFrameEmptyPayload) {
    xgl_frame_t frame;

    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = 0x05;
    params.payload = nullptr;
    params.payload_len = 0;
    params.reliable = false;
    params.priority = 0;

    xgl_error_t result = xgl_frame_build(&frame, &params);

    EXPECT_EQ(result, XGL_OK);
    EXPECT_EQ(frame.payload, nullptr);
    EXPECT_EQ(frame.payload_len, 0);
    EXPECT_EQ(frame.header.payload_len, 0);
}

TEST(XglFrameTest, BuildFrameRejectsPayloadLargerThanWireLength) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01};
    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = 0x05;
    params.payload = payload;
    params.payload_len = 65536;
    params.reliable = false;
    params.priority = 0;

    EXPECT_EQ(xgl_frame_build(&frame, &params), XGL_ERR_BUFFER_TOO_SMALL);
}

/*---------------------------------------------------------------------------*/
/* Frame Serialization Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, SerializeFrame) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t buffer[256];
    size_t bytes_written;

    /* Build frame */
    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = 0x05;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.priority = 3;
    xgl_frame_build(&frame, &params);

    /* Serialize */
    xgl_error_t result =
        xgl_frame_serialize(buffer, sizeof(buffer), &frame, &bytes_written);

    EXPECT_EQ(result, XGL_OK);

    /* Expected size: Header(24) + Payload(4) + CRC16(2) = 30 */
    size_t expected_size = xgl_frame_calculate_size(sizeof(payload));
    EXPECT_EQ(bytes_written, expected_size);
    EXPECT_EQ(bytes_written, 30);

    EXPECT_EQ(buffer[0], XGL_WIRE_MAGIC_0);
    EXPECT_EQ(buffer[1], XGL_WIRE_MAGIC_1);

    EXPECT_EQ(buffer[XGL_FRAME_HEADER_SIZE + 0], 0xAA);
    EXPECT_EQ(buffer[XGL_FRAME_HEADER_SIZE + 1], 0xBB);
    EXPECT_EQ(buffer[XGL_FRAME_HEADER_SIZE + 2], 0xCC);
    EXPECT_EQ(buffer[XGL_FRAME_HEADER_SIZE + 3], 0xDD);
}

TEST(XglFrameTest, BuildFrameKeepsApplicationTypeOutOfPacketType) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0xAA};

    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = XGL_PACKET_TYPE_ACK;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.priority = 3;

    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    EXPECT_EQ(frame.header.packet_type, XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(frame.header.traffic_class & XGL_RELIABILITY_CLASS_MASK,
              XGL_RELIABILITY_ACK_ELICITING);
    EXPECT_EQ(frame.header.traffic_class & XGL_TRAFFIC_PRIORITY_MASK, 3U);
}

TEST(XglFrameTest, SerializeFramePreserves16BitNodeIds) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0xAA};
    uint8_t buffer[256] = {};
    size_t bytes_written = 0;

    xgl_frame_params_t params = {};
    params.source_id = 0x1234;
    params.target_id = 0x2345;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 1;

    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    ASSERT_EQ(
        xgl_frame_serialize(buffer, sizeof(buffer), &frame, &bytes_written),
        XGL_OK);

    xgl_wire_header_t decoded = {};
    ASSERT_EQ(xgl_wire_decode_header(&decoded, buffer, XGL_FRAME_HEADER_SIZE),
              XGL_OK);
    EXPECT_EQ(decoded.source_id, 0x1234);
    EXPECT_EQ(decoded.target_id, 0x2345);
}

TEST(XglFrameTest, SerializeAuthenticatedFrameAddsSecurityExtensionAndTrailer) {
    xgl_auth_provider_t provider = {};
    provider.sign = frame_test_auth_sign;
    provider.verify = frame_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;
    xgl_frame_t frame;
    const uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    uint8_t buffer[256] = {};
    size_t bytes_written = 0;

    xgl_frame_params_t params = {};
    params.source_id = 0x1234;
    params.target_id = 0x2345;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    params.priority = 1;

    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    ASSERT_EQ(test_serialize_trusted_frame(buffer, sizeof(buffer), &frame, 7,
                                           &provider, &bytes_written),
              XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, buffer, bytes_written), XGL_OK);
    EXPECT_EQ(header.header_len,
              XGL_WIRE_BASE_HEADER_SIZE + XGL_WIRE_EXT_HEADER_SIZE + 13U);
    EXPECT_EQ(header.payload_len, sizeof(payload));
    EXPECT_NE(header.flags & XGL_WIRE_FLAG_AUTHENTICATED, 0);
    EXPECT_NE(header.flags & XGL_WIRE_FLAG_HAS_EXTENSIONS, 0);

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(
        xgl_wire_ext_cursor_init(&cursor, buffer + XGL_WIRE_BASE_HEADER_SIZE,
                                 header.header_len - XGL_WIRE_BASE_HEADER_SIZE),
        XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_SECURITY);

    uint32_t key_id = 0;
    uint64_t nonce_id = 0;
    uint8_t tag_len = 0;
    ASSERT_EQ(xgl_wire_decode_security_ext_value(ext.value, ext.len, &key_id,
                                                 &nonce_id, &tag_len),
              XGL_OK);
    EXPECT_EQ(key_id, 7U);
    EXPECT_EQ(tag_len, 4U);

    bool valid = false;
    ASSERT_EQ(test_verify_trusted_frame(buffer, bytes_written - XGL_CRC16_SIZE,
                                        header.header_len, header.payload_len,
                                        7, &provider, &valid),
              XGL_OK);
    EXPECT_TRUE(valid);

    uint16_t received_crc =
        xgb_deserialize_u16_le(&buffer[bytes_written - XGL_CRC16_SIZE]);
    EXPECT_EQ(received_crc,
              xgcrc_crc16_modbus(buffer, bytes_written - XGL_CRC16_SIZE));
}

TEST(XglFrameTest, SerializeAuthenticatedFrameRejectsUnspecifiedTagLength) {
    xgl_auth_provider_t provider = {};
    provider.sign = frame_test_auth_sign;
    provider.verify = frame_test_auth_verify;
    provider.tag_len = 0;
    provider.user_data = nullptr;
    xgl_frame_t frame;
    const uint8_t payload[] = {0x10, 0x20};
    uint8_t buffer[128] = {};
    size_t bytes_written = 0;

    xgl_frame_params_t params = {};
    params.source_id = 1;
    params.target_id = 2;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;

    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    EXPECT_EQ(test_serialize_trusted_frame(buffer, sizeof(buffer), &frame, 7,
                                           &provider, &bytes_written),
              XGL_ERR_INVALID_PARAM);
}

TEST(XglFrameTest,
     SerializeAuthenticatedFrameUsesConfiguredTagLengthAndSignsOnce) {
    size_t sign_count = 0;
    xgl_auth_provider_t provider = {};
    provider.sign = frame_test_auth_sign;
    provider.verify = frame_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = &sign_count;
    xgl_frame_t frame;
    const uint8_t payload[] = {0x10, 0x20};
    uint8_t buffer[128] = {};
    size_t bytes_written = 0;

    xgl_frame_params_t params = {};
    params.source_id = 1;
    params.target_id = 2;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;

    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    ASSERT_EQ(test_serialize_trusted_frame(buffer, sizeof(buffer), &frame, 7,
                                           &provider, &bytes_written),
              XGL_OK);

    EXPECT_EQ(sign_count, 1U);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, buffer, bytes_written), XGL_OK);
    EXPECT_EQ(header.header_len,
              XGL_WIRE_BASE_HEADER_SIZE + XGL_WIRE_EXT_HEADER_SIZE + 13U);

    bool valid = false;
    ASSERT_EQ(test_verify_trusted_frame(buffer, bytes_written - XGL_CRC16_SIZE,
                                        header.header_len, header.payload_len,
                                        7, &provider, &valid),
              XGL_OK);
    EXPECT_TRUE(valid);
}

TEST(XglFrameTest,
     AuthenticatedFrameVerificationIgnoresHopMutableHeaderFields) {
    xgl_auth_provider_t provider = {};
    provider.sign = frame_test_auth_sign;
    provider.verify = frame_test_auth_verify;
    provider.tag_len = 4;
    provider.user_data = nullptr;
    xgl_frame_t frame;
    const uint8_t payload[] = {0x10, 0x20};
    uint8_t buffer[128] = {};
    size_t bytes_written = 0;

    xgl_frame_params_t params = {};
    params.source_id = 1;
    params.target_id = 2;
    params.data_type = XGL_PACKET_TYPE_DATA;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;
    params.ttl = 8;

    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    ASSERT_EQ(test_serialize_trusted_frame(buffer, sizeof(buffer), &frame, 7,
                                           &provider, &bytes_written),
              XGL_OK);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, buffer, bytes_written), XGL_OK);
    header.ttl = 3;
    ASSERT_EQ(xgl_wire_encode_header(buffer, bytes_written, &header), XGL_OK);
    uint16_t frame_crc =
        xgcrc_crc16_modbus(buffer, bytes_written - XGL_CRC16_SIZE);
    xgb_serialize_u16_le(&buffer[bytes_written - XGL_CRC16_SIZE], frame_crc);

    bool valid = false;
    ASSERT_EQ(test_verify_trusted_frame(buffer, bytes_written - XGL_CRC16_SIZE,
                                        header.header_len, header.payload_len,
                                        7, &provider, &valid),
              XGL_OK);
    EXPECT_TRUE(valid);
}

TEST(XglFrameTest, SerializeFrameBufferTooSmall) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buffer[10]; /* Too small */
    size_t bytes_written;

    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = 0x05;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;
    xgl_frame_build(&frame, &params);

    xgl_error_t result =
        xgl_frame_serialize(buffer, sizeof(buffer), &frame, &bytes_written);

    EXPECT_EQ(result, XGL_ERR_BUFFER_TOO_SMALL);
}

TEST(XglFrameTest, SerializeFrameNullPointers) {
    xgl_frame_t frame;
    uint8_t buffer[256];
    size_t bytes_written;

    /* Null buffer */
    EXPECT_EQ(xgl_frame_serialize(nullptr, 256, &frame, &bytes_written),
              XGL_ERR_NULL_POINTER);

    /* Null frame */
    EXPECT_EQ(xgl_frame_serialize(buffer, 256, nullptr, &bytes_written),
              XGL_ERR_NULL_POINTER);

    /* Null bytes_written */
    EXPECT_EQ(xgl_frame_serialize(buffer, 256, &frame, nullptr),
              XGL_ERR_NULL_POINTER);
}

TEST(XglFrameTest, SerializeRejectsNonemptyNullSpans) {
    xgl_frame_params_t params = {};
    params.source_id = 1U;
    params.target_id = 2U;
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    uint8_t buffer[64] = {};
    size_t written = 0U;

    frame.payload_len = 1U;
    EXPECT_EQ(xgl_frame_serialize(buffer, sizeof(buffer), &frame, &written),
              XGL_ERR_NULL_POINTER);
    frame.payload_len = 0U;
    frame.extensions_len = XGL_DATA_TYPE_EXT_SIZE;
    EXPECT_EQ(xgl_frame_serialize(buffer, sizeof(buffer), &frame, &written),
              XGL_ERR_NULL_POINTER);
}

TEST(XglFrameTest, SerializeRejectsUnrepresentablePayloadLength) {
    xgl_frame_params_t params = {};
    params.source_id = 1U;
    params.target_id = 2U;
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    uint8_t buffer[64] = {};
    frame.payload_len = SIZE_MAX;
    size_t written = 0U;
    EXPECT_EQ(xgl_frame_serialize(buffer, sizeof(buffer), &frame, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
}

TEST(XglFrameTest, PureEncodingPlansAuthenticatedLayoutWithoutAProvider) {
    const uint8_t payload[] = {0x11, 0x22, 0x33};
    xgl_frame_params_t params = {};
    params.source_id = 1U;
    params.target_id = 2U;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    xgl_frame_layout_t layout = {};
    ASSERT_EQ(xgl_frame_measure(&frame, 4U, &layout), XGL_OK);
    EXPECT_EQ(layout.header_len, 39U);
    EXPECT_EQ(layout.payload_len, 3U);
    EXPECT_EQ(layout.tag_len, 4U);
    EXPECT_EQ(layout.frame_len, 48U);
    uint8_t buffer[48] = {};
    ASSERT_EQ(
        xgl_frame_encode_into(buffer, sizeof(buffer), &frame, 4U, &layout),
        XGL_OK);
    EXPECT_EQ(std::memcmp(buffer + 39U, payload, sizeof(payload)), 0);
    size_t written = 0U;
    ASSERT_EQ(xgl_frame_finalize_crc(buffer, sizeof(buffer), &layout, &written),
              XGL_OK);
    EXPECT_EQ(written, 48U);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_frame(&view, buffer, written, nullptr), XGL_OK);
    EXPECT_TRUE(view.authenticated);
    EXPECT_EQ(view.auth_tag_len, 4U);
}

TEST(XglFrameTest, ZeroCopyAndCopiedFramesUseIdenticalWireRules) {
    const uint8_t payload[] = {0x41, 0x42};
    xgl_frame_params_t params = {};
    params.source_id = 1U;
    params.target_id = 2U;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.packet_number = 99U;
    params.ttl = 8U;
    xgl_frame_t frame = {};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    uint8_t copied[28] = {}, inplace[28] = {};
    size_t copied_length = 0U, inplace_length = 0U;
    ASSERT_EQ(
        xgl_frame_serialize(copied, sizeof(copied), &frame, &copied_length),
        XGL_OK);
    std::memcpy(inplace + 24U, payload, sizeof(payload));
    ASSERT_EQ(xgl_frame_build_zerocopy(inplace, sizeof(inplace), 24U,
                                       sizeof(payload), 1U, 2U, 0U, 99U, false,
                                       0U, &inplace_length),
              XGL_OK);
    EXPECT_EQ(copied_length, inplace_length);
    EXPECT_EQ(std::memcmp(copied, inplace, sizeof(copied)), 0);
}

/*---------------------------------------------------------------------------*/
/* Zero-Copy Tests                                                           */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, BuildZeroCopyFrame) {
    /* Allocate buffer with space for header */
    uint8_t buffer[256];
    size_t data_offset = XGL_FRAME_HEADER_SIZE + XGL_WIRE_EXT_HEADER_SIZE + 1U;
    size_t data_len = 8;
    size_t frame_len;

    /* Write payload data */
    for (size_t i = 0; i < data_len; i++) {
        buffer[data_offset + i] = (uint8_t)(0x10 + i);
    }

    /* Build frame in zero-copy mode */
    xgl_error_t result =
        xgl_frame_build_zerocopy(buffer, sizeof(buffer), data_offset, data_len,
                                 0x10, 0x20, 0x05, 0x42, true, 3, &frame_len);

    EXPECT_EQ(result, XGL_OK);

    /* Expected: Header(27) + Data(8) + CRC16(2) = 37 */
    EXPECT_EQ(frame_len, 37);

    EXPECT_EQ(buffer[0], XGL_WIRE_MAGIC_0);
    EXPECT_EQ(buffer[1], XGL_WIRE_MAGIC_1);

    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, buffer, frame_len), XGL_OK);
    EXPECT_EQ(header.packet_type, XGL_PACKET_TYPE_DATA);
    EXPECT_EQ(header.header_len, data_offset);
    EXPECT_EQ(header.traffic_class & XGL_RELIABILITY_CLASS_MASK,
              XGL_RELIABILITY_ACK_ELICITING);

    xgl_wire_ext_cursor_t cursor = {};
    ASSERT_EQ(
        xgl_wire_ext_cursor_init(&cursor, buffer + XGL_WIRE_BASE_HEADER_SIZE,
                                 header.header_len - XGL_WIRE_BASE_HEADER_SIZE),
        XGL_OK);
    xgl_wire_ext_t ext = {};
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.type, XGL_WIRE_EXT_DATA_TYPE);
    ASSERT_EQ(ext.len, 1U);
    EXPECT_EQ(ext.value[0], 0x05U);

    /* Check payload is intact */
    for (size_t i = 0; i < data_len; i++) {
        EXPECT_EQ(buffer[data_offset + i], (uint8_t)(0x10 + i));
    }
}

TEST(XglFrameTest, ZeroCopyInvalidOffset) {
    uint8_t buffer[256];
    size_t frame_len;

    /* Offset too small (no room for fixed header) */
    xgl_error_t result = xgl_frame_build_zerocopy(
        buffer, sizeof(buffer), 10, /* Too small, needs to be at least 12 */
        8, 0x10, 0x20, 0x05, 0x42, false, 0, &frame_len);

    EXPECT_EQ(result, XGL_ERR_INVALID_PARAM);
}

TEST(XglFrameTest, ZeroCopyBufferTooSmall) {
    uint8_t buffer[20];
    size_t data_offset = XGL_FRAME_HEADER_SIZE + XGL_WIRE_EXT_HEADER_SIZE + 1U;
    size_t frame_len;

    /* Buffer too small for data + CRC16 */
    xgl_error_t result = xgl_frame_build_zerocopy(
        buffer, sizeof(buffer), data_offset, 100, /* Too large */
        0x10, 0x20, 0x05, 0x42, false, 0, &frame_len);

    EXPECT_EQ(result, XGL_ERR_BUFFER_TOO_SMALL);
}

/*---------------------------------------------------------------------------*/
/* CRC Validation Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, ValidateHeaderCRC) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02};

    /* Build frame (header CRC16 is calculated automatically) */
    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = 0x05;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;
    xgl_frame_build(&frame, &params);

    uint8_t buffer[256] = {};
    size_t bytes_written = 0;
    ASSERT_EQ(
        xgl_frame_serialize(buffer, sizeof(buffer), &frame, &bytes_written),
        XGL_OK);

    xgl_wire_header_t decoded = {};
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, bytes_written), XGL_OK);
    EXPECT_EQ(decoded.header_crc16, xgb_deserialize_u16_le(&buffer[22]));
}

TEST(XglFrameTest, InvalidHeaderCRC) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02};

    /* Build frame */
    xgl_frame_params_t params = {};
    params.source_id = 0x10;
    params.target_id = 0x20;
    params.data_type = 0x05;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = false;
    params.priority = 0;
    xgl_frame_build(&frame, &params);

    uint8_t buffer[256] = {};
    size_t bytes_written = 0;
    ASSERT_EQ(
        xgl_frame_serialize(buffer, sizeof(buffer), &frame, &bytes_written),
        XGL_OK);
    ASSERT_GE(bytes_written, XGL_FRAME_HEADER_SIZE);

    buffer[22] ^= 0xFF;

    xgl_wire_header_t decoded = {};
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, XGL_FRAME_HEADER_SIZE),
              XGL_ERR_CRC_FAILED);
}

/*---------------------------------------------------------------------------*/
/* Traffic-Class Helper Tests                                                */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, TrafficClassHelpers) {
    uint8_t traffic_class_bits = 0;

    /* Set reliability */
    xgl_frame_set_reliability(&traffic_class_bits, true);
    EXPECT_EQ(xgl_frame_get_reliability(traffic_class_bits),
              XGL_RELIABILITY_ACK_ELICITING >> XGL_RELIABILITY_CLASS_SHIFT);

    /* Set priority */
    xgl_frame_set_priority(&traffic_class_bits, 5);
    EXPECT_EQ(xgl_frame_get_priority(traffic_class_bits), 5);

    /* Verify reliable is still set */
    EXPECT_EQ(xgl_frame_get_reliability(traffic_class_bits),
              XGL_RELIABILITY_ACK_ELICITING >> XGL_RELIABILITY_CLASS_SHIFT);
}

TEST(XglFrameTest, FrameSizeCalculation) {
    /* Header(24) + Payload(N) + CRC16(2) */
    EXPECT_EQ(xgl_frame_calculate_size(0), 26);
    EXPECT_EQ(xgl_frame_calculate_size(10), 36);
    EXPECT_EQ(xgl_frame_calculate_size(100), 126);
}
