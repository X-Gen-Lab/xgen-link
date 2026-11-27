/**
 * \file            test_coverage_wire.cpp
 * \brief           Wire codec rejection boundaries and extension contracts
 */

#include <datalink/xgl_parser.h>
#include <gtest/gtest.h>
#include <wire/xgl_crc16.h>
#include <wire/xgl_frame.h>
#include <wire/xgl_wire.h>
#include <wire/xgl_wire_bytes.h>

#include <array>
#include <cstring>
#include <vector>

namespace {

xgl_wire_header_t valid_header() {
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = 1;
    header.target_id = 2;
    return header;
}

void refresh_header_crc(uint8_t* bytes) {
    bytes[22] = 0;
    bytes[23] = 0;
    xgl_serialize_u16_le(bytes + 22,
                         xgl_crc16_modbus(bytes, XGL_WIRE_BASE_HEADER_SIZE));
}

}  // namespace

TEST(XglCoverageWire, HeaderRejectsEachInvalidSemanticFieldAndNullArgument) {
    uint8_t bytes[XGL_WIRE_BASE_HEADER_SIZE] = {};
    auto header = valid_header();
    EXPECT_EQ(xgl_wire_encode_header(nullptr, sizeof(bytes), &header),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_header(nullptr, bytes, sizeof(bytes)),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_header(&header, nullptr, sizeof(bytes)),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_header(&header, bytes, sizeof(bytes) - 1),
              XGL_ERR_BUFFER_TOO_SMALL);

    struct InvalidField {
        size_t offset;
        uint8_t value;
    };

    const InvalidField cases[] = {{2, 0},   {3, 0},    {4, 0},
                                  {4, 255}, {5, 0x10}, {7, 0x18}};
    for (const auto& item : cases) {
        SCOPED_TRACE(item.offset);
        header = valid_header();
        ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header),
                  XGL_OK);
        bytes[item.offset] = item.value;
        refresh_header_crc(bytes);
        EXPECT_EQ(xgl_wire_decode_header(&header, bytes, sizeof(bytes)),
                  XGL_ERR_INVALID_FRAME);
        EXPECT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header),
                  XGL_ERR_INVALID_PARAM);
    }
    for (size_t offset : {0U, 1U}) {
        header = valid_header();
        ASSERT_EQ(xgl_wire_encode_header(bytes, sizeof(bytes), &header),
                  XGL_OK);
        bytes[offset] ^= 1;
        EXPECT_EQ(xgl_wire_decode_header(&header, bytes, sizeof(bytes)),
                  XGL_ERR_INVALID_FRAME);
    }
}

TEST(XglCoverageWire, GenericExtensionValidatesLengthPointersAndCursorState) {
    uint8_t bytes[260] = {};
    size_t written = 99;
    EXPECT_EQ(
        xgl_wire_encode_ext(nullptr, sizeof(bytes), 1, bytes, 1, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_ext(bytes, sizeof(bytes), 1, bytes, 1, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_ext(bytes, sizeof(bytes), 0, bytes, 1, &written),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(
        xgl_wire_encode_ext(bytes, sizeof(bytes), 1, bytes, 256, &written),
        XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(
        xgl_wire_encode_ext(bytes, sizeof(bytes), 1, nullptr, 1, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_ext(bytes, 2, 1, bytes, 1, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(
        xgl_wire_encode_ext(bytes, sizeof(bytes), 1, nullptr, 0, &written),
        XGL_OK);
    EXPECT_EQ(written, 2U);
    xgl_wire_ext_cursor_t cursor = {};
    xgl_wire_ext_t ext = {};
    EXPECT_EQ(xgl_wire_ext_cursor_init(nullptr, bytes, 1),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_ext_cursor_init(&cursor, nullptr, 1),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_ext_cursor_next(nullptr, &ext), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_ext_cursor_next(&cursor, nullptr), XGL_ERR_NULL_POINTER);
    ASSERT_EQ(xgl_wire_ext_cursor_init(&cursor, bytes, written), XGL_OK);
    ASSERT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_OK);
    EXPECT_EQ(ext.len, 0U);
    EXPECT_EQ(xgl_wire_ext_cursor_next(&cursor, &ext), XGL_ERR_NOT_FOUND);
    EXPECT_FALSE(ext.valid);
}

TEST(XglCoverageWire,
     FixedWidthExtensionCodecsRejectMissingOutputAndWrongSizes) {
    uint8_t bytes[32] = {};
    size_t written = 0;
    uint32_t a = 0;
    uint64_t wide = 0;
    uint8_t tag = 0;
    EXPECT_EQ(xgl_wire_encode_session_ext_value(nullptr, 32, 1, 2, &written),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_session_ext_value(bytes, 32, 1, 2, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_session_ext_value(bytes, 11, 1, 2, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_wire_decode_session_ext_value(nullptr, 12, &a, &wide),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_session_ext_value(bytes, 12, nullptr, &wide),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_session_ext_value(bytes, 12, &a, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_session_ext_value(bytes, 13, &a, &wide),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(
        xgl_wire_encode_security_ext_value(nullptr, 32, 1, 2, 4, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_security_ext_value(bytes, 32, 1, 2, 4, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_security_ext_value(bytes, 32, 1, 2, 0, &written),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_wire_encode_security_ext_value(bytes, 12, 1, 2, 4, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_wire_decode_security_ext_value(nullptr, 13, &a, &wide, &tag),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_decode_security_ext_value(bytes, 13, nullptr, &wide, &tag),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_security_ext_value(bytes, 13, &a, nullptr, &tag),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_security_ext_value(bytes, 13, &a, &wide, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_security_ext_value(bytes, 12, &a, &wide, &tag),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(xgl_wire_decode_security_ext_value(bytes, 13, &a, &wide, &tag),
              XGL_ERR_INVALID_FRAME);
}

TEST(XglCoverageWire, SingletonMetadataRejectsMalformedAndDuplicateExtensions) {
    xgl_wire_ext_metadata_t metadata = {};
    EXPECT_EQ(xgl_wire_decode_ext_metadata(nullptr, 0, false, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ext_metadata(nullptr, 1, false, &metadata),
              XGL_ERR_NULL_POINTER);
    for (uint8_t type : {XGL_WIRE_EXT_DATA_TYPE, XGL_WIRE_EXT_SESSION,
                         XGL_WIRE_EXT_SECURITY}) {
        SCOPED_TRACE(type);
        const uint8_t wrong[] = {type, 0};
        EXPECT_EQ(xgl_wire_decode_ext_metadata(wrong, sizeof(wrong),
                                               type == XGL_WIRE_EXT_SECURITY,
                                               &metadata),
                  XGL_ERR_INVALID_FRAME);
        uint8_t value[13] = {};
        size_t value_len = type == XGL_WIRE_EXT_DATA_TYPE ? 1U
                           : type == XGL_WIRE_EXT_SESSION ? 12U
                                                          : 13U;
        value[12] = 4;
        std::vector<uint8_t> duplicate;
        for (int repeat = 0; repeat != 2; ++repeat) {
            duplicate.push_back(type);
            duplicate.push_back(static_cast<uint8_t>(value_len));
            duplicate.insert(duplicate.end(), value, value + value_len);
        }
        EXPECT_EQ(xgl_wire_decode_ext_metadata(
                      duplicate.data(), duplicate.size(),
                      type == XGL_WIRE_EXT_SECURITY, &metadata),
                  XGL_ERR_INVALID_FRAME);
    }
}

TEST(XglCoverageWire, AckRangesValidateCapacityAndRoundTripMaximumCount) {
    uint8_t bytes[260] = {};
    std::array<xgl_wire_ack_range_t, 62> ranges = {};
    ranges[0].gap = 7;
    ranges[0].length = 9;
    size_t written = 0;
    xgl_wire_ack_range_view_t view = {};
    EXPECT_EQ(xgl_wire_encode_ack_range_ext_value(nullptr, 260, 1, 2, nullptr,
                                                  0, &written),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_ack_range_ext_value(bytes, 260, 1, 2, nullptr, 0,
                                                  nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_ack_range_ext_value(bytes, 260, 1, 2, nullptr, 1,
                                                  &written),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_ack_range_ext_value(bytes, 260, 1, 2,
                                                  ranges.data(), 62, &written),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_wire_encode_ack_range_ext_value(bytes, 8, 1, 2, nullptr, 0,
                                                  &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(bytes, 260, 1, 2, nullptr, 0,
                                                  &written),
              XGL_OK);
    ASSERT_EQ(xgl_wire_decode_ack_range_view(bytes, written, &view), XGL_OK);
    EXPECT_EQ(view.range_count, 0U);
    EXPECT_EQ(xgl_wire_decode_ack_range_view(nullptr, 9U, &view),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ack_range_view(bytes, 9U, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ack_range_view(bytes, 8U, &view),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(xgl_wire_decode_ack_range_view(bytes, 10U, &view),
              XGL_ERR_INVALID_FRAME);
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(bytes, 260, UINT32_MAX, 31,
                                                  ranges.data(), 61, &written),
              XGL_OK);
    EXPECT_EQ(written, 253U);
    ASSERT_EQ(xgl_wire_decode_ack_range_view(bytes, written, &view), XGL_OK);
    EXPECT_EQ(view.largest_ack, UINT32_MAX);
    EXPECT_EQ(view.ack_delay_us, 31U);
    ASSERT_EQ(view.range_count, 61U);
    xgl_wire_ack_range_t range = {};
    ASSERT_EQ(xgl_wire_ack_range_at(&view, 0U, &range), XGL_OK);
    EXPECT_EQ(range.gap, 7U);
    EXPECT_EQ(range.length, 9U);
}

TEST(XglCoverageWire, SackEmptyAndMaximumBitmapHonorOutputContracts) {
    uint8_t bytes[260] = {}, bitmap[256] = {};
    size_t written = 0;
    xgl_wire_sack_view_t view = {};
    EXPECT_EQ(
        xgl_wire_encode_sack_ext_value(nullptr, 260, 1, nullptr, 0, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_encode_sack_ext_value(bytes, 260, 1, nullptr, 0, nullptr),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_encode_sack_ext_value(bytes, 260, 1, nullptr, 1, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_encode_sack_ext_value(bytes, 260, 1, bitmap, 256, &written),
        XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(
        xgl_wire_encode_sack_ext_value(bytes, 260, 1, bitmap, 251, &written),
        XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_wire_encode_sack_ext_value(bytes, 4, 1, nullptr, 0, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    ASSERT_EQ(
        xgl_wire_encode_sack_ext_value(bytes, 260, 17, nullptr, 0, &written),
        XGL_OK);
    ASSERT_EQ(xgl_wire_decode_sack_view(bytes, written, &view), XGL_OK);
    EXPECT_EQ(view.base_packet, 17U);
    EXPECT_EQ(view.bitmap_len, 0U);
    bitmap[249] = 0xA5;
    ASSERT_EQ(xgl_wire_encode_sack_ext_value(bytes, 260, UINT32_MAX, bitmap,
                                             250, &written),
              XGL_OK);
    ASSERT_EQ(xgl_wire_decode_sack_view(bytes, written, &view), XGL_OK);
    EXPECT_EQ(view.base_packet, UINT32_MAX);
    ASSERT_EQ(view.bitmap_len, 250U);
    EXPECT_EQ(view.bitmap[249], 0xA5U);
}

TEST(XglCoverageWire, AckRangeCountRejectsSizeOverflowBeforeCapacityCheck) {
    uint8_t bytes[9] = {};
    xgl_wire_ack_range_t range = {};
    size_t written = 17;
    EXPECT_EQ(xgl_wire_encode_ack_range_ext_value(bytes, 0, 1, 0, &range,
                                                  SIZE_MAX / 4U + 1U, &written),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(written, 17U);
}

TEST(XglCoverageWire, DecodedFrameClearsBorrowedPointersOnStructuralMismatch) {
    uint8_t bytes[64] = {};
    auto header = valid_header();
    xgl_wire_frame_view_t view = {};
    xgl_wire_decode_status_t status = XGL_WIRE_DECODE_OK;
    EXPECT_EQ(xgl_wire_decode_frame(nullptr, bytes, 26, &status),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
    EXPECT_EQ(xgl_wire_decode_frame(&view, nullptr, 26, &status),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_header(bytes, 23, &header),
              XGL_ERR_BUFFER_TOO_SMALL);
    ASSERT_EQ(xgl_wire_encode_header(bytes, 64, &header), XGL_OK);
    bytes[0] = 0;
    EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, 26, &status),
              XGL_ERR_INVALID_FRAME);
    ASSERT_EQ(xgl_wire_encode_header(bytes, 64, &header), XGL_OK);
    EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, 27, &status),
              XGL_ERR_INVALID_FRAME);
    header.flags = XGL_WIRE_FLAG_AUTHENTICATED;
    ASSERT_EQ(xgl_wire_encode_header(bytes, 64, &header), XGL_OK);
    EXPECT_EQ(xgl_wire_decode_frame(&view, bytes, 26, &status),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(view.frame_buf, nullptr);
    EXPECT_EQ(view.payload, nullptr);
    header.flags = XGL_WIRE_FLAG_ACK_ELICITING;
    header.traffic_class = XGL_RELIABILITY_ACK_ELICITING;
    ASSERT_EQ(xgl_wire_encode_header(bytes, 64, &header), XGL_OK);
    xgl_serialize_u16_le(bytes + 24, xgl_crc16_modbus(bytes, 24));
    ASSERT_EQ(xgl_wire_decode_frame(&view, bytes, 26, &status), XGL_OK);
    EXPECT_EQ(view.reliable, XGL_RELIABILITY_ACK_ELICITING);
}

TEST(XglCoverageWire,
     FrameBuildersPreserveExplicitClassAndRejectOversizedExtensions) {
    uint8_t bytes[300] = {}, extensions[240] = {};
    xgl_frame_t frame = {};
    xgl_frame_params_t params = {};
    params.source_id = 1;
    params.target_id = 2;
    params.packet_type = XGL_PACKET_TYPE_DATA;
    params.packet_number = 73;
    params.reliability_class = XGL_RELIABILITY_ACK_ELICITING;
    params.priority = 3U;
    params.extensions = extensions;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    EXPECT_EQ(frame.header.packet_number, 73U);
    EXPECT_EQ(frame.header.traffic_class, 0x43U);
    size_t written = 0;
    frame.extensions_len = 232;
    EXPECT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    frame.extensions_len = 0;
    frame.payload = bytes;
    ASSERT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &written),
              XGL_OK);
    EXPECT_EQ(written, 26U);
    frame.header.traffic_class = 0xC0U;
    EXPECT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &written),
              XGL_ERR_INVALID_PARAM);
}

TEST(XglCoverageWire,
     ParserRejectsMissingOutputsAndResynchronizesRepeatedMagic) {
    xgl_parser_t parser = {};
    uint8_t cache[64] = {}, *frame = nullptr;
    size_t length = 0;
    EXPECT_EQ(xgl_parser_feed_byte(nullptr, 0, 0), XGL_PARSE_RESULT_ERROR);
    EXPECT_FALSE(xgl_parser_check_timeout(nullptr, 0, 1));
    ASSERT_EQ(xgl_parser_init(&parser, cache, sizeof(cache)), XGL_OK);
    EXPECT_EQ(xgl_parser_get_frame(&parser, &frame, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_parser_get_frame(&parser, &frame, &length),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_0, 0),
              XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_0, 0),
              XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(xgl_parser_feed_byte(&parser, XGL_WIRE_MAGIC_1, 0),
              XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(parser.cache_len, 2U);
}
