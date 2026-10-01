/**
 * \file            test_wire_rx_contracts.cpp
 * \brief           Complete-frame failure and stream validation contracts
 */

#include <datalink/xgl_parser.h>
#include <wire/xgl_wire.h>

#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

namespace {

std::vector<uint8_t> make_frame(const std::vector<uint8_t>& extensions = {}) {
    std::vector<uint8_t> bytes(XGL_WIRE_BASE_HEADER_SIZE + extensions.size() +
                               2U + XGL_CRC16_SIZE);
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len =
        static_cast<uint8_t>(XGL_WIRE_BASE_HEADER_SIZE + extensions.size());
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.flags = XGL_WIRE_FLAG_ACK_ELICITING;
    header.source_id = 0x1234U;
    header.target_id = 0x5678U;
    header.connection_id = 9U;
    header.packet_number = 17U;
    header.payload_len = 2U;
    EXPECT_EQ(xgl_wire_encode_header(bytes.data(), bytes.size(), &header),
              XGL_OK);
    if (!extensions.empty()) {
        std::memcpy(bytes.data() + XGL_WIRE_BASE_HEADER_SIZE, extensions.data(),
                    extensions.size());
    }
    bytes[header.header_len] = 0x42U;
    bytes[header.header_len + 1U] = 0x87U;
    xgb_serialize_u16_le(
        bytes.data() + bytes.size() - XGL_CRC16_SIZE,
        xgcrc_crc16_modbus(bytes.data(), bytes.size() - XGL_CRC16_SIZE));
    return bytes;
}

void expect_cleared_failure(const uint8_t* bytes, size_t size) {
    xgl_wire_frame_view_t view;
    std::memset(&view, 0xA5, sizeof(view));
    EXPECT_NE(xgl_wire_decode_frame(&view, bytes, size, nullptr), XGL_OK);
    const std::array<uint8_t, sizeof(view)> cleared = {};
    EXPECT_EQ(std::memcmp(&view, cleared.data(), cleared.size()), 0);
}

}  // namespace

TEST(XglWireRxContractTest, EveryTruncationAndNullInputClearTheEntireView) {
    const auto bytes = make_frame({XGL_WIRE_EXT_DATA_TYPE, 1U, 7U});
    for (size_t size = 0U; size < bytes.size(); ++size) {
        SCOPED_TRACE(size);
        expect_cleared_failure(bytes.data(), size);
    }
    expect_cleared_failure(nullptr, bytes.size());
}

TEST(XglWireRxContractTest, CorruptionAndInvalidMetadataClearTheEntireView) {
    auto bytes = make_frame();
    for (size_t index = 0U; index < bytes.size(); ++index) {
        SCOPED_TRACE(index);
        bytes[index] ^= 0x01U;
        expect_cleared_failure(bytes.data(), bytes.size());
        bytes[index] ^= 0x01U;
    }
    bytes = make_frame(
        {XGL_WIRE_EXT_DATA_TYPE, 1U, 7U, XGL_WIRE_EXT_DATA_TYPE, 1U, 8U});
    expect_cleared_failure(bytes.data(), bytes.size());
    bytes = make_frame({XGL_WIRE_EXT_SESSION, 1U, 0U});
    expect_cleared_failure(bytes.data(), bytes.size());
}

TEST(XglWireRxContractTest, StreamAndRawPathsExposeTheSameValidatedFields) {
    const auto bytes =
        make_frame({XGL_WIRE_EXT_DATA_TYPE, 1U, 7U, 250U, 2U, 0x12U, 0x34U});
    xgl_wire_frame_view_t raw = {};
    ASSERT_EQ(xgl_wire_decode_frame(&raw, bytes.data(), bytes.size(), nullptr),
              XGL_OK);
    std::array<uint8_t, 128> cache = {};
    xgl_parser_t parser = {};
    ASSERT_EQ(xgl_parser_init(&parser, cache.data(), cache.size()), XGL_OK);
    xgl_wire_frame_view_t stream = {};
    xgl_wire_decode_status_t status = XGL_WIRE_DECODE_INVALID;
    for (size_t index = 0U; index < bytes.size(); ++index) {
        const auto result = xgl_parser_feed_byte_view(&parser, bytes[index],
                                                      10U, &stream, &status);
        ASSERT_EQ(result, index + 1U == bytes.size()
                              ? XGL_PARSE_RESULT_COMPLETE
                              : XGL_PARSE_RESULT_INCOMPLETE);
    }
    EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
    EXPECT_EQ(stream.header.source_id, raw.header.source_id);
    EXPECT_EQ(stream.header.target_id, raw.header.target_id);
    EXPECT_EQ(stream.header.connection_id, raw.header.connection_id);
    EXPECT_EQ(stream.header.packet_number, raw.header.packet_number);
    EXPECT_EQ(stream.header.header_crc16, raw.header.header_crc16);
    EXPECT_EQ(stream.data_type, raw.data_type);
    EXPECT_EQ(stream.reliable, raw.reliable);
    EXPECT_EQ(stream.frame_len, raw.frame_len);
    EXPECT_EQ(stream.payload_len, raw.payload_len);
    EXPECT_EQ(stream.frame_buf, cache.data());
    ASSERT_NE(stream.payload, nullptr);
    EXPECT_EQ(std::memcmp(stream.payload, raw.payload, raw.payload_len), 0);
}

TEST(XglWireRxContractTest, StreamKeepsCrcClassificationAndRecoversAfterReset) {
    const auto valid = make_frame();
    std::array<uint8_t, 128> cache = {};
    xgl_parser_t parser = {};
    ASSERT_EQ(xgl_parser_init(&parser, cache.data(), cache.size()), XGL_OK);
    for (const bool corrupt_header : {true, false}) {
        auto bytes = valid;
        bytes[corrupt_header ? 6U : XGL_WIRE_BASE_HEADER_SIZE] ^= 1U;
        xgl_wire_frame_view_t view = {};
        xgl_wire_decode_status_t status = XGL_WIRE_DECODE_OK;
        xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
        for (const uint8_t byte : bytes) {
            result =
                xgl_parser_feed_byte_view(&parser, byte, 10U, &view, &status);
            if (result == XGL_PARSE_RESULT_ERROR) {
                break;
            }
        }
        EXPECT_EQ(result, XGL_PARSE_RESULT_ERROR);
        EXPECT_EQ(status, corrupt_header ? XGL_WIRE_DECODE_HEADER_CRC
                                         : XGL_WIRE_DECODE_FRAME_CRC);
        EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
        for (const uint8_t byte : valid) {
            result =
                xgl_parser_feed_byte_view(&parser, byte, 20U, &view, &status);
        }
        EXPECT_EQ(result, XGL_PARSE_RESULT_COMPLETE);
        EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
        xgl_parser_reset(&parser);
    }
}
