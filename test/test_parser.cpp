/**
 * \file            test_parser.cpp
 * \brief           Frame parser unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_parser.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_wire.h>
#include <vector>

static xgl_error_t parser_test_auth_sign(uint32_t key_id,
                                         const uint8_t* aad,
                                         size_t aad_len,
                                         const uint8_t* payload,
                                         size_t payload_len,
                                         uint8_t* tag,
                                         size_t tag_capacity,
                                         size_t* tag_len,
                                         void* user_data) {
    (void)user_data;
    if (tag == nullptr || tag_len == nullptr || tag_capacity < 4U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint32_t acc = key_id;
    for (size_t i = 0; i < aad_len; ++i) {
        acc = (acc * 33U) ^ aad[i];
    }
    for (size_t i = 0; i < payload_len; ++i) {
        acc = (acc * 33U) ^ payload[i];
    }

    xgl_serialize_u32_le(tag, acc);
    *tag_len = 4U;
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Test Fixtures                                                             */
/*---------------------------------------------------------------------------*/

class XglParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Initialize cache buffer */
        cache_buffer.resize(1024);
        
        /* Initialize parser */
        xgl_error_t err = xgl_parser_init(&parser, cache_buffer.data(), cache_buffer.size());
        ASSERT_EQ(err, XGL_OK);
    }
    
    void TearDown() override {
        /* Cleanup */
    }
    
    /* Helper: Create a valid frame */
    std::vector<uint8_t> create_valid_frame(const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> frame;
        
        /* Build frame structure */
        xgl_frame_t xgl_frame;
        xgl_frame_params_t params = {
            .source_id = 0x01,
            .target_id = 0x02,
            .data_type = 0x03,
            .payload = payload.data(),
            .payload_len = payload.size(),
            .reliable = true,
            .priority = 5
        };
        
        xgl_error_t err = xgl_frame_build(&xgl_frame, &params);
        EXPECT_EQ(err, XGL_OK);
        
        /* Serialize to buffer */
        std::vector<uint8_t> buffer(1024);
        size_t bytes_written = 0;
        err = xgl_frame_serialize(buffer.data(), buffer.size(), &xgl_frame, &bytes_written);
        EXPECT_EQ(err, XGL_OK);
        
        /* Copy to result */
        frame.assign(buffer.begin(), buffer.begin() + bytes_written);
        return frame;
    }

    std::vector<uint8_t> create_wire_frame_with_ext(const std::vector<uint8_t>& ext,
                                                    const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> frame(XGL_WIRE_BASE_HEADER_SIZE + ext.size() +
                                   payload.size() + XGL_CRC16_SIZE);

        xgl_wire_header_t header = {
            .version = XGL_WIRE_VERSION,
            .header_len = static_cast<uint8_t>(XGL_WIRE_BASE_HEADER_SIZE + ext.size()),
            .packet_type = XGL_PACKET_TYPE_DATA,
            .flags = static_cast<uint8_t>(XGL_WIRE_FLAG_HAS_EXTENSIONS),
            .ttl = 8,
            .traffic_class = 3,
            .source_id = 0x1234,
            .target_id = 0x5678,
            .connection_id = 0x01020304,
            .packet_number = 0x10203040,
            .payload_len = static_cast<uint16_t>(payload.size()),
            .header_crc16 = 0
        };

        EXPECT_EQ(xgl_wire_encode_header(frame.data(), frame.size(), &header), XGL_OK);

        size_t offset = XGL_WIRE_BASE_HEADER_SIZE;
        if (!ext.empty()) {
            memcpy(&frame[offset], ext.data(), ext.size());
            offset += ext.size();
        }
        if (!payload.empty()) {
            memcpy(&frame[offset], payload.data(), payload.size());
            offset += payload.size();
        }

        uint16_t crc16 = xgl_crc16_modbus(frame.data(), offset);
        xgl_serialize_u16_le(&frame[offset], crc16);
        return frame;
    }
    
    xgl_parser_t parser;
    std::vector<uint8_t> cache_buffer;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglParserTest, InitSuccess) {
    xgl_parser_t test_parser;
    std::vector<uint8_t> buffer(256);
    
    xgl_error_t err = xgl_parser_init(&test_parser, buffer.data(), buffer.size());
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(test_parser.state, XGL_PARSE_MAGIC);
    EXPECT_EQ(test_parser.cache_len, 0);
}

TEST_F(XglParserTest, InitNullPointer) {
    std::vector<uint8_t> buffer(256);
    
    xgl_error_t err = xgl_parser_init(nullptr, buffer.data(), buffer.size());
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
    
    xgl_parser_t test_parser;
    err = xgl_parser_init(&test_parser, nullptr, buffer.size());
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglParserTest, InitBufferTooSmall) {
    xgl_parser_t test_parser;
    std::vector<uint8_t> buffer(10);  /* Too small */
    
    xgl_error_t err = xgl_parser_init(&test_parser, buffer.data(), buffer.size());
    EXPECT_EQ(err, XGL_ERR_BUFFER_TOO_SMALL);
}

/*---------------------------------------------------------------------------*/
/* Reset Tests                                                               */
/*---------------------------------------------------------------------------*/

TEST_F(XglParserTest, ResetParser) {
    /* Feed some data */
    parser.state = XGL_PARSE_HEADER;
    parser.cache_len = 5;
    parser.index = 3;
    parser.timestamp = 1000;
    
    /* Reset */
    xgl_parser_reset(&parser);
    
    /* Verify reset state */
    EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
    EXPECT_EQ(parser.cache_len, 0);
    EXPECT_EQ(parser.index, 0);
    EXPECT_EQ(parser.timestamp, 0);
}

TEST_F(XglParserTest, ResetNullPointer) {
    /* Should not crash */
    xgl_parser_reset(nullptr);
}

/*---------------------------------------------------------------------------*/
/* Magic Detection Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST_F(XglParserTest, FindMagic) {
    /* Feed garbage bytes */
    xgl_parse_result_t result;
    result = xgl_parser_feed_byte(&parser, 0x00, 0);
    EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
    
    result = xgl_parser_feed_byte(&parser, 0xFF, 0);
    EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
    
    /* Feed production magic */
    result = xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_0, 0);
    EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(parser.state, XGL_PARSE_HEADER);
    EXPECT_EQ(parser.cache_len, 1);
}

TEST_F(XglParserTest, ResyncsOverlappingMagicAfterNoisePrefix) {
    std::vector<uint8_t> payload = {0x21, 0x22, 0x23};
    std::vector<uint8_t> frame = create_valid_frame(payload);

    xgl_parse_result_t result = xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_0, 0);
    ASSERT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);

    for (size_t i = 0; i < frame.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame[i], 0);
    }

    EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);

    uint8_t* frame_buffer = nullptr;
    size_t frame_len = 0;
    ASSERT_EQ(xgl_parser_get_frame(&parser, &frame_buffer, &frame_len), XGL_OK);
    EXPECT_EQ(frame_len, frame.size());
    EXPECT_EQ(frame_buffer[0], XGL_WIRE_MAGIC_0);
    EXPECT_EQ(frame_buffer[1], XGL_WIRE_MAGIC_1);
}

/*---------------------------------------------------------------------------*/
/* Complete Frame Parsing Tests                                             */
/*---------------------------------------------------------------------------*/

TEST_F(XglParserTest, ParseCompleteFrameNoPayload) {
    /* Create frame with no payload */
    std::vector<uint8_t> payload;
    std::vector<uint8_t> frame = create_valid_frame(payload);
    
    /* Feed frame byte by byte */
    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    for (size_t i = 0; i < frame.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame[i], 0);
        if (i < frame.size() - 1) {
            EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);
        }
    }
    
    /* Last byte should complete the frame */
    EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);
    
    /* Verify we can get the frame */
    uint8_t* frame_buffer = nullptr;
    size_t frame_len = 0;
    xgl_error_t err = xgl_parser_get_frame(&parser, &frame_buffer, &frame_len);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(frame_len, frame.size());
}

TEST_F(XglParserTest, ParseCompleteFrameWithPayload) {
    /* Create frame with payload */
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05};
    std::vector<uint8_t> frame = create_valid_frame(payload);
    
    /* Feed frame byte by byte */
    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    for (size_t i = 0; i < frame.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame[i], 0);
        if (i < frame.size() - 1) {
            EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);
        }
    }
    
    /* Last byte should complete the frame */
    EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);
    
    /* Verify frame data */
    uint8_t* frame_buffer = nullptr;
    size_t frame_len = 0;
    xgl_error_t err = xgl_parser_get_frame(&parser, &frame_buffer, &frame_len);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(frame_len, frame.size());
    
    /* Verify frame content matches */
    for (size_t i = 0; i < frame.size(); ++i) {
        EXPECT_EQ(frame_buffer[i], frame[i]);
    }
}

TEST_F(XglParserTest, ParseCompleteFrameWithExtensionsAndPayload) {
    uint8_t ack_value[32] = {};
    size_t ack_value_len = 0;
    const xgl_wire_ack_range_t ranges[] = {
        {.gap = 0, .length = 3},
        {.gap = 2, .length = 1}
    };
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(ack_value, sizeof(ack_value),
                                                  42, 250, ranges, 2,
                                                  &ack_value_len),
              XGL_OK);

    std::vector<uint8_t> ext(64);
    size_t ext_len = 0;
    ASSERT_EQ(xgl_wire_encode_ext(ext.data(), ext.size(), XGL_WIRE_EXT_ACK_RANGE,
                                  ack_value, ack_value_len, &ext_len),
              XGL_OK);
    ext.resize(ext_len);

    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> frame = create_wire_frame_with_ext(ext, payload);

    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    for (size_t i = 0; i < frame.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame[i], 0);
        if (i < frame.size() - 1) {
            EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);
        }
    }

    EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);

    uint8_t* frame_buffer = nullptr;
    size_t frame_len = 0;
    ASSERT_EQ(xgl_parser_get_frame(&parser, &frame_buffer, &frame_len), XGL_OK);
    EXPECT_EQ(frame_len, frame.size());
    EXPECT_EQ(frame_buffer[3], XGL_WIRE_BASE_HEADER_SIZE + ext.size());
}

TEST_F(XglParserTest, ParseCompleteAuthenticatedFrameWithSecurityTrailer) {
    xgl_auth_provider_t provider = {
        .sign = parser_test_auth_sign,
        .verify = nullptr,
        .tag_len = 4,
        .user_data = nullptr
    };
    const std::vector<uint8_t> payload = {0x11, 0x22, 0x33};
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {
        .source_id = 0x1234,
        .target_id = 0x5678,
        .data_type = XGL_PACKET_TYPE_DATA,
        .payload = payload.data(),
        .payload_len = payload.size(),
        .reliable = true,
        .priority = 2
    };
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);

    std::vector<uint8_t> encoded(128);
    size_t encoded_len = 0;
    ASSERT_EQ(xgl_frame_serialize_authenticated(encoded.data(),
                                                encoded.size(),
                                                &frame,
                                                7,
                                                &provider,
                                                &encoded_len),
              XGL_OK);
    encoded.resize(encoded_len);

    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    for (size_t i = 0; i < encoded.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, encoded[i], 0);
        if (i < encoded.size() - 1U) {
            EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);
        }
    }

    EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);
    uint8_t* frame_buffer = nullptr;
    size_t frame_len = 0;
    ASSERT_EQ(xgl_parser_get_frame(&parser, &frame_buffer, &frame_len), XGL_OK);
    EXPECT_EQ(frame_len, encoded.size());
}

TEST_F(XglParserTest, InvalidExtensionLengthResetsParser) {
    std::vector<uint8_t> invalid_ext = {
        XGL_WIRE_EXT_ACK_RANGE,
        12,
        0x01,
        0x02
    };
    std::vector<uint8_t> frame = create_wire_frame_with_ext(invalid_ext, {});

    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    bool error_detected = false;
    for (size_t i = 0; i < frame.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame[i], 0);
        if (result == XGL_PARSE_RESULT_ERROR) {
            error_detected = true;
            EXPECT_EQ(i, XGL_WIRE_BASE_HEADER_SIZE + invalid_ext.size() - 1U);
            EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
            break;
        }
    }

    EXPECT_TRUE(error_detected);
}

TEST_F(XglParserTest, ParseCompleteFrameLargePayload) {
    /* Create frame with large payload */
    std::vector<uint8_t> payload(256);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> frame = create_valid_frame(payload);
    
    /* Feed frame byte by byte */
    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    for (size_t i = 0; i < frame.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame[i], 0);
    }
    
    /* Should complete successfully */
    EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);
}

/*---------------------------------------------------------------------------*/
/* Error Handling Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglParserTest, InvalidHeaderCRC16) {
    /* Create valid frame */
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    std::vector<uint8_t> frame = create_valid_frame(payload);
    
    /* Corrupt header CRC16 */
    frame[22] ^= 0xFF;
    
    /* Feed frame byte by byte */
    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    bool error_detected = false;
    for (size_t i = 0; i < frame.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame[i], 0);
        
        /* Parser validates header CRC16 when the fixed header is complete. */
        if (result == XGL_PARSE_RESULT_ERROR) {
            error_detected = true;
            EXPECT_EQ(i, XGL_FRAME_HEADER_SIZE - 1U);
            EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);  /* Reset to magic search */
            break;
        }
    }
    EXPECT_TRUE(error_detected);  /* Ensure error was detected */
}

TEST_F(XglParserTest, InvalidCRC16) {
    /* Create valid frame */
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    std::vector<uint8_t> frame = create_valid_frame(payload);
    
    /* Corrupt CRC16 (last 2 bytes) */
    frame[frame.size() - 1] ^= 0xFF;
    
    /* Feed frame byte by byte */
    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    for (size_t i = 0; i < frame.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame[i], 0);
    }
    
    /* Should detect CRC16 error */
    EXPECT_EQ(result, XGL_PARSE_RESULT_ERROR);
    EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);  /* Reset to magic search */
}

TEST_F(XglParserTest, FrameTooLarge) {
    /* Create parser with small buffer */
    xgl_parser_t small_parser;
    std::vector<uint8_t> small_buffer(50);  /* Small buffer */
    xgl_error_t err = xgl_parser_init(&small_parser, small_buffer.data(), small_buffer.size());
    ASSERT_EQ(err, XGL_OK);
    
    /* Create frame with payload that won't fit */
    std::vector<uint8_t> payload(100);  /* Too large */
    std::vector<uint8_t> frame = create_valid_frame(payload);
    
    /* Feed frame byte by byte */
    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    bool error_detected = false;
    for (size_t i = 0; i < frame.size() && i < XGL_FRAME_HEADER_SIZE; ++i) {
        result = xgl_parser_feed_byte(&small_parser, frame[i], 0);
        
        /* Parser detects frame too large when header is complete (after byte 11) */
        if (result == XGL_PARSE_RESULT_ERROR) {
            error_detected = true;
            EXPECT_EQ(i, XGL_FRAME_HEADER_SIZE - 1U);
            EXPECT_EQ(small_parser.state, XGL_PARSE_MAGIC);
            break;
        }
    }
    EXPECT_TRUE(error_detected);  /* Ensure error was detected */
}

/*---------------------------------------------------------------------------*/
/* Timeout Tests                                                             */
/*---------------------------------------------------------------------------*/

TEST_F(XglParserTest, TimeoutDetection) {
    /* Start parsing */
    xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_0, 1000);
    EXPECT_EQ(parser.state, XGL_PARSE_HEADER);
    
    /* Check timeout - not expired */
    bool timeout = xgl_parser_check_timeout(&parser, 1500, XGL_PARSER_TIMEOUT_MS);
    EXPECT_FALSE(timeout);
    
    /* Check timeout - expired */
    timeout = xgl_parser_check_timeout(&parser, 2500, XGL_PARSER_TIMEOUT_MS);
    EXPECT_TRUE(timeout);
}

TEST_F(XglParserTest, NoTimeoutWhenIdle) {
    /* Parser in magic-search state (idle) */
    EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
    
    /* Should never timeout when idle */
    bool timeout = xgl_parser_check_timeout(&parser, 999999, XGL_PARSER_TIMEOUT_MS);
    EXPECT_FALSE(timeout);
}

/*---------------------------------------------------------------------------*/
/* State Machine Tests                                                       */
/*---------------------------------------------------------------------------*/

TEST_F(XglParserTest, StateTransitions) {
    /* Initial state */
    EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
    
    /* Feed magic -> HEADER */
    xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_0, 0);
    EXPECT_EQ(parser.state, XGL_PARSE_HEADER);
    
    /* Create valid frame to continue */
    std::vector<uint8_t> payload = {0x01, 0x02};
    std::vector<uint8_t> frame = create_valid_frame(payload);
    
    /* Feed header bytes (first magic byte already fed) */
    for (size_t i = 1; i < XGL_FRAME_HEADER_SIZE; ++i) {
        xgl_parser_feed_byte(&parser, frame[i], 0);
    }
    
    /* Should transition to PAYLOAD */
    EXPECT_EQ(parser.state, XGL_PARSE_PAYLOAD);
    
    /* Feed payload bytes */
    for (size_t i = 0; i < payload.size(); ++i) {
        xgl_parser_feed_byte(&parser, frame[XGL_FRAME_HEADER_SIZE + i], 0);
    }
    
    /* Should transition to CRC */
    EXPECT_EQ(parser.state, XGL_PARSE_CRC);
}

/*---------------------------------------------------------------------------*/
/* Edge Cases                                                                */
/*---------------------------------------------------------------------------*/

TEST_F(XglParserTest, MultipleFrames) {
    /* Parse first frame */
    std::vector<uint8_t> payload1 = {0x01, 0x02};
    std::vector<uint8_t> frame1 = create_valid_frame(payload1);
    
    xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
    for (size_t i = 0; i < frame1.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame1[i], 0);
    }
    EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);
    
    /* Reset for next frame */
    xgl_parser_reset(&parser);
    
    /* Parse second frame */
    std::vector<uint8_t> payload2 = {0x03, 0x04, 0x05};
    std::vector<uint8_t> frame2 = create_valid_frame(payload2);
    
    result = XGL_PARSE_RESULT_INCOMPLETE;
    for (size_t i = 0; i < frame2.size(); ++i) {
        result = xgl_parser_feed_byte(&parser, frame2[i], 0);
    }
    EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);
}

TEST_F(XglParserTest, GetFrameBeforeComplete) {
    /* Try to get frame before parsing is complete */
    uint8_t* frame_buffer = nullptr;
    size_t frame_len = 0;
    xgl_error_t err = xgl_parser_get_frame(&parser, &frame_buffer, &frame_len);
    EXPECT_EQ(err, XGL_ERR_INVALID_FRAME);
}

TEST_F(XglParserTest, GetFrameNullPointers) {
    xgl_error_t err = xgl_parser_get_frame(nullptr, nullptr, nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
    
    uint8_t* frame_buffer = nullptr;
    err = xgl_parser_get_frame(&parser, &frame_buffer, nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

