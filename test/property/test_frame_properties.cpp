/**
 * \file            test_frame_properties.cpp
 * \brief           Frame handling property tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_parser.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_wire.h>
#include "property_framework.h"
#include <vector>
#include <cstring>

/*---------------------------------------------------------------------------*/
/* Property 2: CRC Error Detection                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 2: CRC Error Detection
 * \details         For any frame with corrupted CRC (either header CRC16 or frame CRC16),
 *                  the protocol should reject it and increment the appropriate
 *                  error counter.
 *                  Validates: Requirements 3.3, 3.4, 13.4
 */
TEST(XglFrameProperties, CrcErrorDetection) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random frame parameters */
        uint8_t source_id = static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
        uint8_t target_id = static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
        uint8_t data_type = XGL_PACKET_TYPE_DATA;
        uint8_t seq_num = gen.random_uint8();
        uint8_t ack_num = gen.random_uint8();
        bool reliable = (gen.random_uint8() & 1) != 0;
        uint8_t priority = gen.random_uint8() & 0x07;  /* 3 bits */
        
        /* Generate random payload (0 to 256 bytes) */
        size_t payload_len = gen.random_uint32() % 257;
        std::vector<uint8_t> payload = gen.random_bytes(payload_len);
        
        /* Build frame */
        xgl_frame_t frame;
        xgl_frame_params_t params = {
            .source_id = source_id,
            .target_id = target_id,
            .data_type = data_type,
            .seq_num = seq_num,
            .ack_num = ack_num,
            .payload = payload.data(),
            .payload_len = payload_len,
            .reliable = reliable,
            .priority = priority
        };
        
        xgl_error_t err = xgl_frame_build(&frame, &params);
        ASSERT_EQ(err, XGL_OK);
        
        /* Serialize frame to buffer */
        std::vector<uint8_t> buffer(xgl_frame_calculate_size(payload_len));
        size_t bytes_written = 0;
        err = xgl_frame_serialize(buffer.data(), buffer.size(), &frame, &bytes_written);
        ASSERT_EQ(err, XGL_OK);
        
        /* Test header CRC16 corruption detection */
        {
            std::vector<uint8_t> corrupted = buffer;
            corrupted[22] ^= 0x01;  /* Flip one bit in header CRC16 */
            
            xgl_wire_header_t header = {};
            EXPECT_EQ(xgl_wire_decode_header(&header,
                                             corrupted.data(),
                                             XGL_FRAME_HEADER_SIZE),
                      XGL_ERR_CRC_FAILED)
                << "Header CRC16 corruption not detected at iteration " << iteration;
        }
        
        /* Test CRC16 corruption detection */
        {
            std::vector<uint8_t> corrupted = buffer;
            /* Corrupt the CRC16 bytes (last 2 bytes) */
            size_t crc16_offset = bytes_written - 2;
            corrupted[crc16_offset] ^= 0x01;  /* Flip one bit in CRC16 */
            
            /* Parse the corrupted frame */
            xgl_parser_t parser;
            std::vector<uint8_t> cache(1024);
            err = xgl_parser_init(&parser, cache.data(), cache.size());
            ASSERT_EQ(err, XGL_OK);
            
            /* Feed all bytes to parser */
            xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
            for (size_t i = 0; i < bytes_written; ++i) {
                result = xgl_parser_feed_byte(&parser, corrupted[i], 0);
                if (result != XGL_PARSE_RESULT_INCOMPLETE) {
                    break;
                }
            }
            
            /* Parser should detect CRC16 error */
            EXPECT_EQ(result, XGL_PARSE_RESULT_ERROR)
                << "CRC16 corruption not detected at iteration " << iteration;
        }
        
        /* Test that valid frame passes both CRC checks */
        {
            xgl_wire_header_t header = {};
            EXPECT_EQ(xgl_wire_decode_header(&header,
                                             buffer.data(),
                                             XGL_FRAME_HEADER_SIZE),
                      XGL_OK)
                << "Valid header CRC16 rejected at iteration " << iteration;
            
            /* Parse the valid frame */
            xgl_parser_t parser;
            std::vector<uint8_t> cache(1024);
            err = xgl_parser_init(&parser, cache.data(), cache.size());
            ASSERT_EQ(err, XGL_OK);
            
            /* Feed all bytes to parser */
            xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
            for (size_t i = 0; i < bytes_written; ++i) {
                result = xgl_parser_feed_byte(&parser, buffer[i], 0);
                if (result != XGL_PARSE_RESULT_INCOMPLETE) {
                    break;
                }
            }
            
            /* Parser should accept valid frame */
            EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE)
                << "Valid frame rejected at iteration " << iteration;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Property 5: Frame Encapsulation Round-Trip                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 5: Frame Encapsulation Round-Trip
 * \details         For any valid packet data, encapsulating it into a frame
 *                  and then parsing the frame should produce equivalent packet data.
 *                  Validates: Requirements 3.1, 3.2, 3.5
 */
TEST(XglFrameProperties, FrameEncapsulationRoundTrip) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random frame parameters */
        uint8_t source_id = static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
        uint8_t target_id = static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
        uint8_t data_type = XGL_PACKET_TYPE_DATA;
        uint8_t seq_num = gen.random_uint8();
        uint8_t ack_num = gen.random_uint8();
        bool reliable = (gen.random_uint8() & 1) != 0;
        uint8_t priority = gen.random_uint8() & 0x07;  /* 3 bits */
        
        /* Generate random payload (0 to 512 bytes) */
        size_t payload_len = gen.random_uint32() % 513;
        std::vector<uint8_t> payload = gen.random_bytes(payload_len);
        
        /* Build frame */
        xgl_frame_t frame;
        xgl_frame_params_t params = {
            .source_id = source_id,
            .target_id = target_id,
            .data_type = data_type,
            .seq_num = seq_num,
            .ack_num = ack_num,
            .payload = payload.data(),
            .payload_len = payload_len,
            .reliable = reliable,
            .priority = priority
        };
        
        xgl_error_t err = xgl_frame_build(&frame, &params);
        ASSERT_EQ(err, XGL_OK);
        
        /* Serialize frame to buffer */
        std::vector<uint8_t> buffer(xgl_frame_calculate_size(payload_len));
        size_t bytes_written = 0;
        err = xgl_frame_serialize(buffer.data(), buffer.size(), &frame, &bytes_written);
        ASSERT_EQ(err, XGL_OK);
        
        /* Parse the serialized frame */
        xgl_parser_t parser;
        std::vector<uint8_t> cache(2048);
        err = xgl_parser_init(&parser, cache.data(), cache.size());
        ASSERT_EQ(err, XGL_OK);
        
        /* Feed all bytes to parser */
        xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
        for (size_t i = 0; i < bytes_written; ++i) {
            result = xgl_parser_feed_byte(&parser, buffer[i], 0);
            if (result != XGL_PARSE_RESULT_INCOMPLETE) {
                break;
            }
        }
        
        /* Parser should complete successfully */
        ASSERT_EQ(result, XGL_PARSE_RESULT_COMPLETE)
            << "Frame parsing failed at iteration " << iteration;
        
        /* Get parsed frame data */
        uint8_t* parsed_buffer = nullptr;
        size_t parsed_len = 0;
        err = xgl_parser_get_frame(&parser, &parsed_buffer, &parsed_len);
        ASSERT_EQ(err, XGL_OK);
        ASSERT_EQ(parsed_len, bytes_written);
        
        xgl_wire_header_t parsed_header = {};
        ASSERT_EQ(xgl_wire_decode_header(&parsed_header,
                                         parsed_buffer,
                                         XGL_FRAME_HEADER_SIZE),
                  XGL_OK);
        
        /* Verify all header fields match */
        EXPECT_EQ(parsed_buffer[0], XGL_WIRE_MAGIC_0)
            << "Magic byte 0 mismatch at iteration " << iteration;
        EXPECT_EQ(parsed_buffer[1], XGL_WIRE_MAGIC_1)
            << "Magic byte 1 mismatch at iteration " << iteration;
        EXPECT_EQ(parsed_header.version, XGL_WIRE_VERSION)
            << "Version mismatch at iteration " << iteration;
        EXPECT_EQ(parsed_header.packet_type, data_type)
            << "Data type mismatch at iteration " << iteration;
        EXPECT_EQ(parsed_header.source_id, source_id)
            << "Source ID mismatch at iteration " << iteration;
        EXPECT_EQ(parsed_header.target_id, target_id)
            << "Target ID mismatch at iteration " << iteration;
        EXPECT_EQ(parsed_header.packet_number, seq_num)
            << "Sequence number mismatch at iteration " << iteration;
        EXPECT_EQ(parsed_header.payload_len, payload_len)
            << "Payload length mismatch at iteration " << iteration;
        
        uint8_t expected_reliable = reliable ? XGL_WIRE_FLAG_ACK_ELICITING : 0U;
        EXPECT_EQ(parsed_header.flags & XGL_WIRE_FLAG_ACK_ELICITING, expected_reliable)
            << "Reliable flag mismatch at iteration " << iteration;
        
        EXPECT_EQ(parsed_header.traffic_class & XGL_ATTR_PRIORITY_MASK, priority)
            << "Priority mismatch at iteration " << iteration;
        
        /* Verify payload data */
        if (payload_len > 0) {
            const uint8_t* parsed_payload = parsed_buffer + XGL_FRAME_HEADER_SIZE;
            EXPECT_EQ(memcmp(parsed_payload, payload.data(), payload_len), 0)
                << "Payload data mismatch at iteration " << iteration;
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Property 27: Field Validation                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 27: Field Validation
 * \details         For any received frame, the protocol should validate all
 *                  field values (version, data_type, etc.) before processing.
 *                  Validates: Requirements 12.3
 */
TEST(XglFrameProperties, FieldValidation) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random but valid frame parameters */
        uint8_t source_id = static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
        uint8_t target_id = static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
        uint8_t data_type = XGL_PACKET_TYPE_DATA;
        uint8_t seq_num = gen.random_uint8();
        uint8_t ack_num = gen.random_uint8();
        bool reliable = (gen.random_uint8() & 1) != 0;
        uint8_t priority = gen.random_uint8() & 0x07;  /* Valid: 3 bits (0-7) */
        
        /* Generate random payload */
        size_t payload_len = gen.random_uint32() % 257;
        std::vector<uint8_t> payload = gen.random_bytes(payload_len);
        
        /* Build and serialize frame */
        xgl_frame_t frame;
        xgl_frame_params_t params = {
            .source_id = source_id,
            .target_id = target_id,
            .data_type = data_type,
            .seq_num = seq_num,
            .ack_num = ack_num,
            .payload = payload.data(),
            .payload_len = payload_len,
            .reliable = reliable,
            .priority = priority
        };
        
        xgl_error_t err = xgl_frame_build(&frame, &params);
        ASSERT_EQ(err, XGL_OK);
        
        std::vector<uint8_t> buffer(xgl_frame_calculate_size(payload_len));
        size_t bytes_written = 0;
        err = xgl_frame_serialize(buffer.data(), buffer.size(), &frame, &bytes_written);
        ASSERT_EQ(err, XGL_OK);
        
        xgl_wire_header_t header = {};
        ASSERT_EQ(xgl_wire_decode_header(&header,
                                         buffer.data(),
                                         XGL_FRAME_HEADER_SIZE),
                  XGL_OK);
        
        EXPECT_EQ(buffer[0], XGL_WIRE_MAGIC_0)
            << "Magic byte 0 validation failed at iteration " << iteration;
        EXPECT_EQ(buffer[1], XGL_WIRE_MAGIC_1)
            << "Magic byte 1 validation failed at iteration " << iteration;
        
        EXPECT_EQ(header.version, XGL_WIRE_VERSION)
            << "Version out of range at iteration " << iteration;
        
        EXPECT_LE(header.packet_type, 0x7F)
            << "Data type out of range at iteration " << iteration;
        EXPECT_EQ(header.packet_type, data_type)
            << "Data type mismatch at iteration " << iteration;
        
        uint8_t parsed_priority = (uint8_t)(header.traffic_class & XGL_ATTR_PRIORITY_MASK);
        EXPECT_LE(parsed_priority, 0x07)
            << "Priority out of range at iteration " << iteration;
        EXPECT_EQ(parsed_priority, priority)
            << "Priority mismatch at iteration " << iteration;
        
        uint8_t expected_reliable = reliable ? XGL_WIRE_FLAG_ACK_ELICITING : 0U;
        EXPECT_EQ(header.flags & XGL_WIRE_FLAG_ACK_ELICITING, expected_reliable)
            << "Reliable flag mismatch at iteration " << iteration;
        
        /* Validate data length matches payload */
        EXPECT_EQ(header.payload_len, payload_len)
            << "Data length validation failed at iteration " << iteration;
        
        /* Validate CRC16 */
        size_t crc16_offset = bytes_written - 2;
        uint16_t calculated_crc16 = xgl_crc16_modbus(buffer.data(), crc16_offset);
        uint16_t received_crc16 = xgl_deserialize_u16_le(&buffer[crc16_offset]);
        EXPECT_EQ(calculated_crc16, received_crc16)
            << "CRC16 validation failed at iteration " << iteration;
    }
}

/*---------------------------------------------------------------------------*/
/* Additional Property: Frame Size Calculation                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Property: Frame size calculation is correct
 * \details         For any payload length, the calculated frame size should
 *                  match the actual serialized frame size.
 */
TEST(XglFrameProperties, FrameSizeCalculation) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random payload length */
        size_t payload_len = gen.random_uint32() % 1024;
        
        /* Calculate expected frame size */
        size_t expected_size = xgl_frame_calculate_size(payload_len);
        
        /* Verify calculation */
        size_t manual_size = XGL_FRAME_HEADER_SIZE + payload_len + XGL_CRC16_SIZE;
        EXPECT_EQ(expected_size, manual_size)
            << "Frame size calculation mismatch at iteration " << iteration
            << " with payload_len=" << payload_len;
        
        /* Build and serialize a frame to verify actual size */
        std::vector<uint8_t> payload = gen.random_bytes(payload_len);
        xgl_frame_t frame;
        xgl_frame_params_t params = {
            .source_id = 1,
            .target_id = 2,
            .data_type = 0,
            .seq_num = 0,
            .ack_num = 0,
            .payload = payload.data(),
            .payload_len = payload_len,
            .reliable = false,
            .priority = 0
        };
        
        xgl_error_t err = xgl_frame_build(&frame, &params);
        ASSERT_EQ(err, XGL_OK);
        
        std::vector<uint8_t> buffer(expected_size + 10);  /* Extra space */
        size_t bytes_written = 0;
        err = xgl_frame_serialize(buffer.data(), buffer.size(), &frame, &bytes_written);
        ASSERT_EQ(err, XGL_OK);
        
        /* Verify actual serialized size matches calculation */
        EXPECT_EQ(bytes_written, expected_size)
            << "Serialized size mismatch at iteration " << iteration
            << " with payload_len=" << payload_len;
    }
}

/*---------------------------------------------------------------------------*/
/* Additional Property: Parser State Machine Robustness                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Property: Parser handles byte-by-byte input correctly
 * \details         For any valid frame, feeding it byte-by-byte to the parser
 *                  should eventually result in a complete frame.
 */
TEST(XglFrameProperties, ParserByteByByteRobustness) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random frame */
        uint8_t source_id = static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
        uint8_t target_id = static_cast<uint8_t>((gen.random_uint8() % 254U) + 1U);
        uint8_t data_type = XGL_PACKET_TYPE_DATA;
        size_t payload_len = gen.random_uint32() % 256;
        std::vector<uint8_t> payload = gen.random_bytes(payload_len);
        
        /* Build and serialize frame */
        xgl_frame_t frame;
        xgl_frame_params_t params = {
            .source_id = source_id,
            .target_id = target_id,
            .data_type = data_type,
            .seq_num = 0,
            .ack_num = 0,
            .payload = payload.data(),
            .payload_len = payload_len,
            .reliable = false,
            .fragment = false,
            .priority = 0
        };
        xgl_error_t err = xgl_frame_build(&frame, &params);
        ASSERT_EQ(err, XGL_OK);
        
        std::vector<uint8_t> buffer(xgl_frame_calculate_size(payload_len));
        size_t bytes_written = 0;
        err = xgl_frame_serialize(buffer.data(), buffer.size(), &frame, &bytes_written);
        ASSERT_EQ(err, XGL_OK);
        
        /* Initialize parser */
        xgl_parser_t parser;
        std::vector<uint8_t> cache(1024);
        err = xgl_parser_init(&parser, cache.data(), cache.size());
        ASSERT_EQ(err, XGL_OK);
        
        /* Feed bytes one at a time */
        xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
        for (size_t i = 0; i < bytes_written; ++i) {
            result = xgl_parser_feed_byte(&parser, buffer[i], static_cast<uint32_t>(i));
            
            /* Should be incomplete until last byte */
            if (i < bytes_written - 1) {
                EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE)
                    << "Parser completed prematurely at iteration " << iteration
                    << " byte " << i << "/" << bytes_written;
            }
        }
        
        /* Final result should be complete */
        EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE)
            << "Parser did not complete at iteration " << iteration;
    }
}

/*---------------------------------------------------------------------------*/
/* Additional Property: Parser Rejects Garbage Data                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Property: Parser rejects random garbage data
 * \details         For any random byte sequence without valid magic,
 *                  the parser should remain in magic search state.
 */
TEST(XglFrameProperties, ParserRejectsGarbageData) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random garbage data (no production magic start byte) */
        size_t garbage_len = 10 + (gen.random_uint32() % 100);
        std::vector<uint8_t> garbage = gen.random_bytes(garbage_len);
        
        /* Ensure no magic start byte in garbage */
        for (size_t i = 0; i < garbage.size(); ++i) {
            if (garbage[i] == XGL_WIRE_MAGIC_0) {
                garbage[i] ^= 0x01;  /* Change it to something else */
            }
        }
        
        /* Initialize parser */
        xgl_parser_t parser;
        std::vector<uint8_t> cache(1024);
        xgl_error_t err = xgl_parser_init(&parser, cache.data(), cache.size());
        ASSERT_EQ(err, XGL_OK);
        
        /* Feed garbage bytes */
        for (size_t i = 0; i < garbage.size(); ++i) {
            xgl_parse_result_t result = xgl_parser_feed_byte(&parser, garbage[i], static_cast<uint32_t>(i));
            
            /* Should always be incomplete (searching for SOF) */
            EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE)
                << "Parser accepted garbage at iteration " << iteration
                << " byte " << i;
        }
        
        /* Parser should still be in SOF state */
        EXPECT_EQ(xgl_parser_get_state(&parser), XGL_PARSE_SOF)
            << "Parser left SOF state after garbage at iteration " << iteration;
    }
}
