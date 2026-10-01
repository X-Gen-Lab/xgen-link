/**
 * \file            test_coverage_wire.cpp
 * \brief           Wire codec rejection boundaries and extension contracts
 */

#include <gtest/gtest.h>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>
#include <wire/xgl_frame.h>
#include <datalink/xgl_parser.h>
#include <wire/xgl_wire.h>

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
    xgb_serialize_u16_le(bytes + 22,
                         xgcrc_crc16_modbus(bytes, XGL_WIRE_BASE_HEADER_SIZE));
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

    const InvalidField cases[] = {{2, 0},
                                  {3, 0},
                                  {4, 0},
                                  {4, 255},
                                  {5, XGL_WIRE_FLAG_ENCRYPTED},
                                  {7, XGL_TRAFFIC_ENCRYPTION_MASK},
                                  {8, 0},
                                  {10, 0}};
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
    uint32_t a = 0, b = 0, c = 0;
    uint64_t wide = 0;
    uint16_t hop = 0, next = 0, metric = 0;
    uint8_t tag = 0;
    EXPECT_EQ(
        xgl_wire_encode_fragment_ext_value(nullptr, 32, 1, 2, 3, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_fragment_ext_value(bytes, 32, 1, 2, 3, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_fragment_ext_value(bytes, 11, 1, 2, 3, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_wire_decode_fragment_ext_value(nullptr, 12, &a, &b, &c),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_fragment_ext_value(bytes, 12, nullptr, &b, &c),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_fragment_ext_value(bytes, 12, &a, nullptr, &c),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_fragment_ext_value(bytes, 12, &a, &b, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_fragment_ext_value(bytes, 13, &a, &b, &c),
              XGL_ERR_INVALID_FRAME);
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
    EXPECT_EQ(
        xgl_wire_encode_route_ext_value(nullptr, 32, 1, 2, 3, 4, &written),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_route_ext_value(bytes, 32, 1, 2, 3, 4, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_encode_route_ext_value(bytes, 9, 1, 2, 3, 4, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(
        xgl_wire_decode_route_ext_value(nullptr, 10, &hop, &next, &a, &metric),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_decode_route_ext_value(bytes, 10, nullptr, &next, &a, &metric),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_decode_route_ext_value(bytes, 10, &hop, nullptr, &a, &metric),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_route_ext_value(bytes, 10, &hop, &next, nullptr,
                                              &metric),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_decode_route_ext_value(bytes, 10, &hop, &next, &a, nullptr),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_decode_route_ext_value(bytes, 11, &hop, &next, &a, &metric),
        XGL_ERR_INVALID_FRAME);
}

TEST(XglCoverageWire, SingletonMetadataRejectsMalformedAndDuplicateExtensions) {
    xgl_wire_ext_metadata_t metadata = {};
    EXPECT_EQ(xgl_wire_decode_ext_metadata(nullptr, 0, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ext_metadata(nullptr, 1, &metadata),
              XGL_ERR_NULL_POINTER);
    for (uint8_t type : {XGL_WIRE_EXT_DATA_TYPE, XGL_WIRE_EXT_SESSION,
                         XGL_WIRE_EXT_SECURITY}) {
        SCOPED_TRACE(type);
        const uint8_t wrong[] = {type, 0};
        EXPECT_EQ(xgl_wire_decode_ext_metadata(wrong, sizeof(wrong), &metadata),
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
        EXPECT_EQ(xgl_wire_decode_ext_metadata(duplicate.data(),
                                               duplicate.size(), &metadata),
                  XGL_ERR_INVALID_FRAME);
    }
}

TEST(XglCoverageWire, AckRangesValidateCapacityAndRoundTripMaximumCount) {
    uint8_t bytes[260] = {};
    std::array<xgl_wire_ack_range_t, 62> ranges = {};
    ranges[0].gap = 7;
    ranges[0].length = 9;
    size_t written = 0, count = 0;
    uint32_t largest = 0, delay = 0;
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
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(bytes, written, &largest,
                                                  &delay, nullptr, 0, &count),
              XGL_OK);
    EXPECT_EQ(count, 0U);
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(nullptr, 9, &largest, &delay,
                                                  ranges.data(), 62, &count),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(bytes, 9, nullptr, &delay,
                                                  ranges.data(), 62, &count),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(bytes, 9, &largest, nullptr,
                                                  ranges.data(), 62, &count),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(bytes, 9, &largest, &delay,
                                                  ranges.data(), 62, nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(bytes, 8, &largest, &delay,
                                                  ranges.data(), 62, &count),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(bytes, 10, &largest, &delay,
                                                  ranges.data(), 62, &count),
              XGL_ERR_INVALID_FRAME);
    ASSERT_EQ(xgl_wire_encode_ack_range_ext_value(bytes, 260, UINT32_MAX, 31,
                                                  ranges.data(), 61, &written),
              XGL_OK);
    EXPECT_EQ(written, 253U);
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(bytes, written, &largest,
                                                  &delay, nullptr, 61, &count),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_ack_range_ext_value(
                  bytes, written, &largest, &delay, ranges.data(), 60, &count),
              XGL_ERR_BUFFER_TOO_SMALL);
    ASSERT_EQ(xgl_wire_decode_ack_range_ext_value(
                  bytes, written, &largest, &delay, ranges.data(), 61, &count),
              XGL_OK);
    EXPECT_EQ(largest, UINT32_MAX);
    EXPECT_EQ(delay, 31U);
    EXPECT_EQ(count, 61U);
    EXPECT_EQ(ranges[0].gap, 7U);
    EXPECT_EQ(ranges[0].length, 9U);
}

TEST(XglCoverageWire, SackEmptyAndMaximumBitmapHonorOutputContracts) {
    uint8_t bytes[260] = {}, bitmap[256] = {};
    size_t written = 0, length = 0;
    uint32_t base = 0;
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
    ASSERT_EQ(xgl_wire_decode_sack_ext_value(bytes, written, &base, nullptr, 0,
                                             &length),
              XGL_OK);
    EXPECT_EQ(base, 17U);
    EXPECT_EQ(length, 0U);
    EXPECT_EQ(
        xgl_wire_decode_sack_ext_value(nullptr, 5, &base, bitmap, 256, &length),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_decode_sack_ext_value(bytes, 5, nullptr, bitmap, 256, &length),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_decode_sack_ext_value(bytes, 5, &base, bitmap, 256, nullptr),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_wire_decode_sack_ext_value(bytes, 4, &base, bitmap, 256, &length),
        XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(
        xgl_wire_decode_sack_ext_value(bytes, 6, &base, bitmap, 256, &length),
        XGL_ERR_INVALID_FRAME);
    bitmap[249] = 0xA5;
    ASSERT_EQ(xgl_wire_encode_sack_ext_value(bytes, 260, UINT32_MAX, bitmap,
                                             250, &written),
              XGL_OK);
    EXPECT_EQ(xgl_wire_decode_sack_ext_value(bytes, written, &base, nullptr,
                                             250, &length),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_sack_ext_value(bytes, written, &base, bitmap, 249,
                                             &length),
              XGL_ERR_BUFFER_TOO_SMALL);
    ASSERT_EQ(xgl_wire_decode_sack_ext_value(bytes, written, &base, bitmap, 250,
                                             &length),
              XGL_OK);
    EXPECT_EQ(base, UINT32_MAX);
    EXPECT_EQ(length, 250U);
    EXPECT_EQ(bitmap[249], 0xA5U);
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
    ASSERT_EQ(xgl_wire_encode_header(bytes, 64, &header), XGL_OK);
    xgb_serialize_u16_le(bytes + 24, xgcrc_crc16_modbus(bytes, 24));
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
    params.flags = XGL_WIRE_FLAG_ACK_ELICITING;
    params.packet_number = 73;
    params.traffic_class = 3;
    params.extensions = extensions;
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    EXPECT_EQ(frame.header.packet_number, 73U);
    EXPECT_EQ(frame.header.traffic_class, 3U);
    size_t written = 0;
    frame.extensions_len = 232;
    EXPECT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    frame.extensions_len = 0;
    frame.payload = bytes;
    ASSERT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &written),
              XGL_OK);
    EXPECT_EQ(written, 26U);
    frame.header.target_id = 0;
    EXPECT_EQ(xgl_frame_serialize(bytes, sizeof(bytes), &frame, &written),
              XGL_ERR_INVALID_PARAM);
}

TEST(XglCoverageWire,
     ZeroCopyBuilderValidatesPointersAndApplicationTypeHeadroom) {
    uint8_t bytes[64] = {};
    size_t written = 0;
    EXPECT_EQ(xgl_frame_build_zerocopy(nullptr, 64, 24, 1, 1, 2, 0, 0, false, 0,
                                       &written),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_frame_build_zerocopy(bytes, 64, 24, 1, 1, 2, 0, 0, false, 0,
                                       nullptr),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_frame_build_zerocopy(bytes, 64, 24, UINT16_MAX + 1U, 1, 2, 0,
                                       0, false, 0, &written),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_frame_build_zerocopy(bytes, 64, 24, 1, 0, 2, 0, 0, false, 0,
                                       &written),
              XGL_ERR_INVALID_PARAM);
    bytes[27] = 0x83;
    ASSERT_EQ(xgl_frame_build_zerocopy(bytes, 64, 27, 1, 1, 2, 9, 0, false, 0,
                                       &written),
              XGL_OK);
    xgl_wire_frame_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_frame(&view, bytes, written, nullptr), XGL_OK);
    EXPECT_EQ(view.data_type, 9U);
    EXPECT_EQ(view.payload[0], 0x83U);
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
