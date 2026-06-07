/**
 * \file            test_frame.cpp
 * \brief           Frame encapsulation unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/xgl_wire.h>
#include <cstring>

/*---------------------------------------------------------------------------*/
/* Frame Building Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, BuildBasicFrame) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    
    xgl_frame_params_t params = {
        .source_id = 0x10,
        .target_id = 0x20,
        .data_type = 0x05,
        .seq_num = 0x42,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = true,
        .priority = 3
    };
    
    xgl_error_t result = xgl_frame_build(&frame, &params);
    
    EXPECT_EQ(result, XGL_OK);
    EXPECT_EQ(frame.header.sof, XGL_SOF);
    EXPECT_EQ(xgl_frame_get_version(&frame.header), 0x01);
    EXPECT_EQ(xgl_frame_get_datatype(&frame.header), 0x05);
    EXPECT_EQ(frame.header.source_id, 0x10);
    EXPECT_EQ(frame.header.target_id, 0x20);
    EXPECT_EQ(frame.header.data_len, sizeof(payload));
    EXPECT_EQ(frame.header.seq_num, 0x42);
    EXPECT_EQ(frame.header.ack_num, 0x00);
    EXPECT_EQ(frame.payload, payload);
    EXPECT_EQ(frame.payload_len, sizeof(payload));
    
    /* Check attributes */
    uint8_t reliable_attr = xgl_frame_get_reliable(frame.header.attr_lsb);
    EXPECT_EQ(reliable_attr, XGL_ATTR_RELIABLE_TX >> XGL_ATTR_RELIABLE_SHIFT);
    
    uint8_t priority = xgl_frame_get_priority(frame.header.attr_lsb);
    EXPECT_EQ(priority, 3);
}

TEST(XglFrameTest, BuildFrameNullPointer) {
    const uint8_t payload[] = {0x01, 0x02};
    
    xgl_frame_params_t params = {
        .source_id = 0x10,
        .target_id = 0x20,
        .data_type = 0x05,
        .seq_num = 0x42,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };
    
    xgl_error_t result = xgl_frame_build(nullptr, &params);
    EXPECT_EQ(result, XGL_ERR_NULL_POINTER);
    
    xgl_frame_t frame;
    result = xgl_frame_build(&frame, nullptr);
    EXPECT_EQ(result, XGL_ERR_NULL_POINTER);
}

TEST(XglFrameTest, BuildFrameEmptyPayload) {
    xgl_frame_t frame;
    
    xgl_frame_params_t params = {
        .source_id = 0x10,
        .target_id = 0x20,
        .data_type = 0x05,
        .seq_num = 0x42,
        .ack_num = 0x00,
        .payload = nullptr,
        .payload_len = 0,
        .reliable = false,
        .priority = 0
    };
    
    xgl_error_t result = xgl_frame_build(&frame, &params);
    
    EXPECT_EQ(result, XGL_OK);
    EXPECT_EQ(frame.payload, nullptr);
    EXPECT_EQ(frame.payload_len, 0);
    EXPECT_EQ(frame.header.data_len, 0);
}

TEST(XglFrameTest, BuildFrameRejectsPayloadLargerThanWireLength) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01};
    xgl_frame_params_t params = {
        .source_id = 0x10,
        .target_id = 0x20,
        .data_type = 0x05,
        .seq_num = 0x42,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = 65536,
        .reliable = false,
        .priority = 0
    };

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
    xgl_frame_params_t params = {
        .source_id = 0x10,
        .target_id = 0x20,
        .data_type = 0x05,
        .seq_num = 0x42,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = true,
        .priority = 3
    };
    xgl_frame_build(&frame, &params);
    
    /* Serialize */
    xgl_error_t result = xgl_frame_serialize(buffer, sizeof(buffer),
                                             &frame, &bytes_written);
    
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

TEST(XglFrameTest, SerializeFrameBufferTooSmall) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t buffer[10];  /* Too small */
    size_t bytes_written;
    
    xgl_frame_params_t params = {
        .source_id = 0x10,
        .target_id = 0x20,
        .data_type = 0x05,
        .seq_num = 0x42,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };
    xgl_frame_build(&frame, &params);
    
    xgl_error_t result = xgl_frame_serialize(buffer, sizeof(buffer),
                                             &frame, &bytes_written);
    
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

/*---------------------------------------------------------------------------*/
/* Header Encoding/Decoding Tests                                           */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, EncodeDecodeHeader) {
    xgl_frame_header_t header_orig, header_decoded;
    uint8_t buffer[XGL_FRAME_HEADER_SIZE];
    
    /* Initialize header */
    memset(&header_orig, 0, sizeof(header_orig));
    header_orig.sof = XGL_SOF;
    xgl_frame_set_version(&header_orig, 0x01);
    xgl_frame_set_datatype(&header_orig, 0x05);
    header_orig.source_id = 0x10;
    header_orig.target_id = 0x20;
    header_orig.attr_lsb = 0x43;  /* Reliable TX + Priority 3 */
    header_orig.attr_msb = 0x00;
    header_orig.data_len = 0x1234;
    header_orig.seq_num = 0x42;
    header_orig.ack_num = 0x99;
    header_orig.reserved = 0x00;
    header_orig.crc8 = 0xAB;
    
    /* Encode */
    xgl_frame_encode_header(buffer, &header_orig);
    
    /* Decode */
    xgl_frame_decode_header(&header_decoded, buffer);
    
    /* Verify */
    EXPECT_EQ(header_decoded.sof, header_orig.sof);
    EXPECT_EQ(xgl_frame_get_version(&header_decoded), xgl_frame_get_version(&header_orig));
    EXPECT_EQ(xgl_frame_get_datatype(&header_decoded), xgl_frame_get_datatype(&header_orig));
    EXPECT_EQ(header_decoded.source_id, header_orig.source_id);
    EXPECT_EQ(header_decoded.target_id, header_orig.target_id);
    EXPECT_EQ(header_decoded.attr_lsb, header_orig.attr_lsb);
    EXPECT_EQ(header_decoded.attr_msb, header_orig.attr_msb);
    EXPECT_EQ(header_decoded.data_len, header_orig.data_len);
    EXPECT_EQ(header_decoded.seq_num, header_orig.seq_num);
    EXPECT_EQ(header_decoded.ack_num, 0);
    EXPECT_EQ(header_decoded.reserved, header_orig.reserved);
    EXPECT_NE(header_decoded.crc8, 0);
}

/*---------------------------------------------------------------------------*/
/* Zero-Copy Tests                                                           */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, BuildZeroCopyFrame) {
    /* Allocate buffer with space for header */
    uint8_t buffer[256];
    size_t data_offset = XGL_FRAME_HEADER_SIZE;  /* Header includes SOF */
    size_t data_len = 8;
    size_t frame_len;
    
    /* Write payload data */
    for (size_t i = 0; i < data_len; i++) {
        buffer[data_offset + i] = (uint8_t)(0x10 + i);
    }
    
    /* Build frame in zero-copy mode */
    xgl_error_t result = xgl_frame_build_zerocopy(
        buffer, sizeof(buffer),
        data_offset, data_len,
        0x10, 0x20, 0x05, 0x42, 0x00,
        true, 3,
        &frame_len
    );
    
    EXPECT_EQ(result, XGL_OK);
    
    /* Expected: Header(24) + Data(8) + CRC16(2) = 34 */
    EXPECT_EQ(frame_len, 34);
    
    EXPECT_EQ(buffer[0], XGL_WIRE_MAGIC_0);
    EXPECT_EQ(buffer[1], XGL_WIRE_MAGIC_1);
    
    /* Check payload is intact */
    for (size_t i = 0; i < data_len; i++) {
        EXPECT_EQ(buffer[data_offset + i], (uint8_t)(0x10 + i));
    }
}

TEST(XglFrameTest, ZeroCopyInvalidOffset) {
    uint8_t buffer[256];
    size_t frame_len;
    
    /* Offset too small (no room for header with SOF) */
    xgl_error_t result = xgl_frame_build_zerocopy(
        buffer, sizeof(buffer),
        10,  /* Too small, needs to be at least 12 */
        8,
        0x10, 0x20, 0x05, 0x42, 0x00,
        false, 0,
        &frame_len
    );
    
    EXPECT_EQ(result, XGL_ERR_INVALID_PARAM);
}

TEST(XglFrameTest, ZeroCopyBufferTooSmall) {
    uint8_t buffer[20];
    size_t data_offset = XGL_FRAME_HEADER_SIZE;
    size_t frame_len;
    
    /* Buffer too small for data + CRC16 */
    xgl_error_t result = xgl_frame_build_zerocopy(
        buffer, sizeof(buffer),
        data_offset,
        100,  /* Too large */
        0x10, 0x20, 0x05, 0x42, 0x00,
        false, 0,
        &frame_len
    );
    
    EXPECT_EQ(result, XGL_ERR_BUFFER_TOO_SMALL);
}

/*---------------------------------------------------------------------------*/
/* CRC Validation Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, ValidateHeaderCRC) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02};
    
    /* Build frame (CRC8 is calculated automatically) */
    xgl_frame_params_t params = {
        .source_id = 0x10,
        .target_id = 0x20,
        .data_type = 0x05,
        .seq_num = 0x42,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };
    xgl_frame_build(&frame, &params);
    
    /* Validate CRC8 */
    bool valid = xgl_frame_validate_header_crc(&frame.header);
    EXPECT_TRUE(valid);
}

TEST(XglFrameTest, InvalidHeaderCRC) {
    xgl_frame_t frame;
    const uint8_t payload[] = {0x01, 0x02};
    
    /* Build frame */
    xgl_frame_params_t params = {
        .source_id = 0x10,
        .target_id = 0x20,
        .data_type = 0x05,
        .seq_num = 0x42,
        .ack_num = 0x00,
        .payload = payload,
        .payload_len = sizeof(payload),
        .reliable = false,
        .priority = 0
    };
    xgl_frame_build(&frame, &params);
    
    uint8_t buffer[256] = {};
    size_t bytes_written = 0;
    ASSERT_EQ(xgl_frame_serialize(buffer, sizeof(buffer), &frame, &bytes_written), XGL_OK);
    ASSERT_GE(bytes_written, XGL_FRAME_HEADER_SIZE);

    buffer[22] ^= 0xFF;

    xgl_wire_header_t decoded = {};
    EXPECT_EQ(xgl_wire_decode_header(&decoded, buffer, XGL_FRAME_HEADER_SIZE),
              XGL_ERR_CRC_FAILED);
}

/*---------------------------------------------------------------------------*/
/* Attribute Helper Tests                                                    */
/*---------------------------------------------------------------------------*/

TEST(XglFrameTest, AttributeHelpers) {
    uint8_t attr_lsb = 0;
    
    /* Set reliable */
    xgl_frame_set_reliable(&attr_lsb, true);
    EXPECT_EQ(xgl_frame_get_reliable(attr_lsb), 
              XGL_ATTR_RELIABLE_TX >> XGL_ATTR_RELIABLE_SHIFT);
    
    /* Set priority */
    xgl_frame_set_priority(&attr_lsb, 5);
    EXPECT_EQ(xgl_frame_get_priority(attr_lsb), 5);
    
    /* Verify reliable is still set */
    EXPECT_EQ(xgl_frame_get_reliable(attr_lsb),
              XGL_ATTR_RELIABLE_TX >> XGL_ATTR_RELIABLE_SHIFT);
}

TEST(XglFrameTest, FrameSizeCalculation) {
    /* Header(24) + Payload(N) + CRC16(2) */
    EXPECT_EQ(xgl_frame_calculate_size(0), 26);
    EXPECT_EQ(xgl_frame_calculate_size(10), 36);
    EXPECT_EQ(xgl_frame_calculate_size(100), 126);
}

