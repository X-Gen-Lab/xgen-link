/**
 * \file            test_parser_resync.cpp
 * \brief           Buffered CRC recovery and frame-consumption contracts
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <datalink/xgl_parser.h>
#include <wire/xgl_frame.h>
#include <wire/xgl_wire.h>

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <vector>
#include <wire/xgl_crc16.h>
#include <wire/xgl_wire_bytes.h>

namespace {

std::vector<uint8_t> make_frame(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> bytes(XGL_WIRE_BASE_HEADER_SIZE + payload.size() +
                               XGL_CRC16_SIZE);
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.source_id = 1U;
    header.target_id = 2U;
    header.payload_len = static_cast<uint16_t>(payload.size());
    EXPECT_EQ(xgl_wire_encode_header(bytes.data(), bytes.size(), &header),
              XGL_OK);
    std::copy(payload.begin(), payload.end(),
              bytes.begin() + XGL_WIRE_BASE_HEADER_SIZE);
    const size_t crc_offset = bytes.size() - XGL_CRC16_SIZE;
    xgl_serialize_u16_le(bytes.data() + crc_offset,
                         xgl_crc16_modbus(bytes.data(), crc_offset));
    return bytes;
}

std::vector<uint8_t> rejected_envelope(const std::vector<uint8_t>& contents) {
    if (contents.size() < XGL_CRC16_SIZE) {
        ADD_FAILURE() << "Envelope contents must include the final CRC bytes";
        return {};
    }
    auto bytes =
        make_frame(std::vector<uint8_t>(contents.size() - XGL_CRC16_SIZE));
    std::copy(contents.begin(), contents.end(),
              bytes.begin() + XGL_WIRE_BASE_HEADER_SIZE);
    xgl_wire_frame_view_t view = {};
    xgl_wire_decode_status_t status = XGL_WIRE_DECODE_INVALID;
    EXPECT_EQ(xgl_wire_decode_frame(&view, bytes.data(), bytes.size(), &status),
              XGL_ERR_CRC_FAILED);
    EXPECT_EQ(status, XGL_WIRE_DECODE_FRAME_CRC);
    return bytes;
}

std::vector<uint8_t>
make_frame_with_security(const std::vector<uint8_t>& payload) {
    const std::array<uint8_t, 3> extensions = {XGL_WIRE_EXT_DATA_TYPE, 1U, 7U};
    xgl_frame_t frame = {};
    frame.header.version = XGL_WIRE_VERSION;
    frame.header.packet_type = XGL_PACKET_TYPE_DATA;
    frame.header.source_id = 1U;
    frame.header.target_id = 2U;
    frame.extensions = extensions.data();
    frame.extensions_len = extensions.size();
    frame.payload = payload.data();
    frame.payload_len = payload.size();
    constexpr uint8_t tag_length = 16U;
    xgl_frame_layout_t layout = {};
    EXPECT_EQ(xgl_frame_measure(&frame, tag_length, &layout), XGL_OK);
    std::vector<uint8_t> bytes(layout.frame_len);
    EXPECT_EQ(xgl_frame_encode_into(bytes.data(), bytes.size(), &frame,
                                    tag_length, &layout),
              XGL_OK);
    const size_t security_value_offset = XGL_WIRE_BASE_HEADER_SIZE +
                                         extensions.size() +
                                         XGL_WIRE_EXT_HEADER_SIZE;
    size_t written = 0U;
    EXPECT_EQ(xgl_wire_encode_security_ext_value(
                  bytes.data() + security_value_offset,
                  XGL_SECURITY_EXT_VALUE_SIZE, 0x12345678U,
                  UINT64_C(0x0102030405060708), tag_length, &written),
              XGL_OK);
    std::fill_n(bytes.data() + layout.header_len + payload.size(), tag_length,
                0x6CU);
    EXPECT_EQ(
        xgl_frame_finalize_crc(bytes.data(), bytes.size(), &layout, &written),
        XGL_OK);
    EXPECT_EQ(written, bytes.size());
    return bytes;
}

class XglParserResyncTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_EQ(xgl_parser_init(&parser, cache.data(), cache.size()), XGL_OK);
    }

    void Drain(xgl_parse_result_t result) {
        size_t events = 0U;
        while (result != XGL_PARSE_RESULT_INCOMPLETE) {
            ASSERT_LT(events++, cache.size() + 1U);
            if (result == XGL_PARSE_RESULT_ERROR) {
                errors.push_back(status);
            } else {
                ASSERT_EQ(result, XGL_PARSE_RESULT_COMPLETE);
                EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
                delivered.emplace_back(view.payload,
                                       view.payload + view.payload_len);
            }
            result = xgl_parser_advance_view(&parser, &view, &status);
        }
    }

    void Feed(const std::vector<uint8_t>& bytes, uint32_t now = 0U) {
        for (const uint8_t byte : bytes) {
            Drain(
                xgl_parser_feed_byte_view(&parser, byte, now, &view, &status));
            if (::testing::Test::HasFatalFailure()) {
                return;
            }
        }
    }

    xgl_parse_result_t FeedUntilLastByte(const std::vector<uint8_t>& bytes,
                                         uint32_t now = 0U) {
        xgl_parse_result_t result = XGL_PARSE_RESULT_INCOMPLETE;
        for (size_t i = 0U; i < bytes.size(); ++i) {
            result = xgl_parser_feed_byte_view(&parser, bytes[i], now, &view,
                                               &status);
            if (i + 1U < bytes.size()) {
                EXPECT_EQ(result, XGL_PARSE_RESULT_INCOMPLETE);
            }
        }
        return result;
    }

    void ExpectCurrentFrame(const std::vector<uint8_t>& expected) {
        uint8_t* bytes = nullptr;
        size_t length = 0U;
        ASSERT_EQ(xgl_parser_get_frame(&parser, &bytes, &length), XGL_OK);
        ASSERT_NE(bytes, nullptr);
        ASSERT_EQ(length, expected.size());
        EXPECT_TRUE(std::equal(expected.begin(), expected.end(), bytes));
        EXPECT_EQ(view.frame_buf, bytes);
        EXPECT_EQ(view.frame_len, length);
    }

    std::array<uint8_t, 256> cache = {};
    xgl_parser_t parser = {};
    xgl_wire_frame_view_t view = {};
    xgl_wire_decode_status_t status = XGL_WIRE_DECODE_INVALID;
    std::vector<xgl_wire_decode_status_t> errors;
    std::vector<std::vector<uint8_t>> delivered;
};

TEST_F(XglParserResyncTest, MissingFinalCrcByteRetainsTheFollowingMagicPrefix) {
    auto truncated = make_frame({0x11U});
    ASSERT_NE(truncated.back(), XGL_WIRE_MAGIC_0);
    truncated.pop_back();
    const auto next = make_frame({0x22U});
    truncated.insert(truncated.end(), next.begin(), next.end());
    Feed(truncated);
    EXPECT_EQ(errors, std::vector<xgl_wire_decode_status_t>(
                          {XGL_WIRE_DECODE_FRAME_CRC}));
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x22U}}));
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 0U);
}

TEST_F(XglParserResyncTest, MissingBothCrcBytesRetainsTheFollowingMagicPair) {
    auto truncated = make_frame({0x11U});
    ASSERT_NE(xgl_deserialize_u16_le(truncated.data() + truncated.size() -
                                     XGL_CRC16_SIZE),
              0x5AA5U);
    truncated.resize(truncated.size() - XGL_CRC16_SIZE);
    const auto next = make_frame({0x22U});
    truncated.insert(truncated.end(), next.begin(), next.end());
    Feed(truncated);
    EXPECT_EQ(errors, std::vector<xgl_wire_decode_status_t>(
                          {XGL_WIRE_DECODE_FRAME_CRC}));
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x22U}}));
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 0U);
}

TEST_F(XglParserResyncTest,
       CrcFailureWithoutAnotherMagicDefersDiscardUntilAdvance) {
    auto invalid = make_frame({0x11U});
    invalid.back() ^= 1U;
    ASSERT_EQ(std::count(invalid.begin() + 1U, invalid.end(), XGL_WIRE_MAGIC_0),
              0);
    ASSERT_EQ(FeedUntilLastByte(invalid), XGL_PARSE_RESULT_ERROR);
    EXPECT_EQ(status, XGL_WIRE_DECODE_FRAME_CRC);
    EXPECT_EQ(parser.state, XGL_PARSE_RECOVER);
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), invalid.size());
    EXPECT_TRUE(std::equal(invalid.begin(), invalid.end(), cache.data()));

    EXPECT_EQ(xgl_parser_advance_view(&parser, &view, &status),
              XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
    EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 0U);
}

TEST_F(XglParserResyncTest, BadHeaderRetainsANewHeaderAlreadyInsideTheCache) {
    std::vector<uint8_t> stream(11U, 0x33U);
    stream[0] = XGL_WIRE_MAGIC_0;
    stream[1] = XGL_WIRE_MAGIC_1;
    const auto next = make_frame({0x22U});
    stream.insert(stream.end(), next.begin(), next.end());
    xgl_wire_header_t header = {};
    ASSERT_EQ(xgl_wire_decode_header(&header, stream.data(),
                                     XGL_WIRE_BASE_HEADER_SIZE),
              XGL_ERR_CRC_FAILED);
    Feed(stream);
    EXPECT_EQ(errors, std::vector<xgl_wire_decode_status_t>(
                          {XGL_WIRE_DECODE_HEADER_CRC}));
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x22U}}));
}

TEST_F(XglParserResyncTest,
       AdvanceDeliversBufferedFramesWithoutInputAndPreservesThePartialTail) {
    const auto first = make_frame({0x31U});
    const auto second = make_frame({0x32U});
    const auto third = make_frame({0x33U});
    constexpr size_t partial_length = 13U;
    std::vector<uint8_t> contents = first;
    contents.insert(contents.end(), second.begin(), second.end());
    contents.insert(contents.end(), third.data(),
                    third.data() + partial_length);
    const auto envelope = rejected_envelope(contents);
    ASSERT_EQ(FeedUntilLastByte(envelope), XGL_PARSE_RESULT_ERROR);
    EXPECT_EQ(status, XGL_WIRE_DECODE_FRAME_CRC);

    ASSERT_EQ(xgl_parser_advance_view(&parser, &view, &status),
              XGL_PARSE_RESULT_COMPLETE);
    EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
    ExpectCurrentFrame(first);
    EXPECT_GT(xgl_parser_get_cached_len(&parser), view.frame_len);

    ASSERT_EQ(xgl_parser_advance_view(&parser, &view, &status),
              XGL_PARSE_RESULT_COMPLETE);
    ExpectCurrentFrame(second);
    ASSERT_EQ(xgl_parser_advance_view(&parser, &view, &status),
              XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), partial_length);
    uint8_t* bytes = nullptr;
    size_t length = 0U;
    EXPECT_EQ(xgl_parser_get_frame(&parser, &bytes, &length),
              XGL_ERR_INVALID_FRAME);

    Feed(std::vector<uint8_t>(third.data() + partial_length,
                              third.data() + third.size()),
         1U);
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x33U}}));
    EXPECT_TRUE(errors.empty());
}

TEST_F(XglParserResyncTest,
       CompleteFrameWinsOverAnEarlierIncompleteLongCandidate) {
    auto contents = make_frame(std::vector<uint8_t>(200U, 0x11U));
    contents.resize(XGL_WIRE_BASE_HEADER_SIZE);
    const auto valid = make_frame({0x22U});
    contents.insert(contents.end(), valid.begin(), valid.end());
    Feed(rejected_envelope(contents));
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x22U}}));
    EXPECT_EQ(errors, std::vector<xgl_wire_decode_status_t>(
                          {XGL_WIRE_DECODE_FRAME_CRC}));
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 0U);
}

TEST_F(XglParserResyncTest,
       EarliestPartialCandidateIsRetainedWhenNoCompleteFrameExists) {
    const auto later = make_frame({0x55U});
    constexpr size_t later_prefix = 13U;
    std::vector<uint8_t> payload(80U, 0x11U);
    std::copy_n(later.begin(), later_prefix, payload.begin());
    const auto first = make_frame(payload);
    const size_t retained = XGL_WIRE_BASE_HEADER_SIZE + later_prefix;
    Feed(rejected_envelope(
        std::vector<uint8_t>(first.data(), first.data() + retained)));
    ASSERT_TRUE(delivered.empty());
    ASSERT_EQ(xgl_parser_get_cached_len(&parser), retained);
    EXPECT_TRUE(
        std::equal(first.data(), first.data() + retained, cache.data()));
    Feed(std::vector<uint8_t>(first.data() + retained,
                              first.data() + first.size()),
         1U);
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({payload}));
    EXPECT_EQ(errors, std::vector<xgl_wire_decode_status_t>(
                          {XGL_WIRE_DECODE_FRAME_CRC}));
}

TEST_F(XglParserResyncTest, RecoveredPrefixesInheritTheOriginalDeadline) {
    const auto next = make_frame({0x22U});
    for (const uint32_t started : {0U, UINT32_MAX - 10U}) {
        for (const size_t prefix : {1U, 2U, 13U}) {
            SCOPED_TRACE(::testing::Message()
                         << "start=" << started << " prefix=" << prefix);
            xgl_parser_reset(&parser);
            errors.clear();
            std::vector<uint8_t> contents(3U, 0x33U);
            contents.insert(contents.end(), next.data(), next.data() + prefix);
            const auto envelope = rejected_envelope(contents);
            Feed({envelope[0]}, started);
            Feed(std::vector<uint8_t>(envelope.data() + 1U,
                                      envelope.data() + envelope.size()),
                 started + 15U);
            ASSERT_EQ(xgl_parser_get_cached_len(&parser), prefix);
            EXPECT_FALSE(xgl_parser_check_timeout(&parser, started + 19U, 20U));
            EXPECT_TRUE(xgl_parser_check_timeout(&parser, started + 20U, 20U));
            EXPECT_FALSE(xgl_parser_check_timeout(&parser, started + 20U, 0U));
            Feed({next[prefix]}, started + 19U);
            EXPECT_TRUE(xgl_parser_check_timeout(&parser, started + 20U, 20U));
        }
    }
    EXPECT_TRUE(delivered.empty());
}

TEST_F(XglParserResyncTest, ExactCapacityRecoveryRetainsTheNextMagicByte) {
    const auto first = make_frame({0x31U});
    const auto second = make_frame({0x32U});
    auto contents = first;
    contents.push_back(second[0]);
    const auto envelope = rejected_envelope(contents);
    ASSERT_EQ(xgl_parser_init(&parser, cache.data(), envelope.size()), XGL_OK);
    Feed(envelope);
    ASSERT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x31U}}));
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 1U);
    Feed(
        std::vector<uint8_t>(second.data() + 1U, second.data() + second.size()),
        1U);
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x31U}, {0x32U}}));
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 0U);
}

TEST_F(XglParserResyncTest, RecoverySkipsMultipleFalseMagicCandidates) {
    auto bad_header = make_frame({0x11U});
    bad_header.resize(XGL_WIRE_BASE_HEADER_SIZE);
    bad_header[22] ^= 1U;
    std::vector<uint8_t> contents = {XGL_WIRE_MAGIC_0, XGL_WIRE_MAGIC_0, 0x33U};
    contents.insert(contents.end(), bad_header.begin(), bad_header.end());
    contents.push_back(0x44U);
    contents.insert(contents.end(), bad_header.begin(), bad_header.end());
    const auto valid = make_frame({0x22U});
    contents.insert(contents.end(), valid.begin(), valid.end());
    Feed(rejected_envelope(contents));
    ASSERT_FALSE(errors.empty());
    EXPECT_EQ(errors.front(), XGL_WIRE_DECODE_FRAME_CRC);
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x22U}}));
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 0U);
}

TEST_F(XglParserResyncTest, AdvanceValidatesPointersWithoutReleasingTheFrame) {
    status = XGL_WIRE_DECODE_OK;
    EXPECT_EQ(xgl_parser_advance_view(nullptr, &view, &status),
              XGL_PARSE_RESULT_ERROR);
    EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
    EXPECT_EQ(xgl_parser_advance_view(&parser, &view, nullptr),
              XGL_PARSE_RESULT_INCOMPLETE);
    const auto valid = make_frame({0x22U});
    ASSERT_EQ(FeedUntilLastByte(valid), XGL_PARSE_RESULT_COMPLETE);
    EXPECT_EQ(xgl_parser_advance_view(&parser, nullptr, &status),
              XGL_PARSE_RESULT_ERROR);
    EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
    ExpectCurrentFrame(valid);
    EXPECT_EQ(xgl_parser_advance_view(&parser, &view, &status),
              XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 0U);
}

TEST_F(XglParserResyncTest,
       FeedRejectsPendingFramesAndErrorsWithoutConsumption) {
    const auto valid = make_frame({0x22U});
    for (const bool recovering : {false, true}) {
        SCOPED_TRACE(recovering);
        xgl_parser_reset(&parser);
        const auto input = recovering ? rejected_envelope(valid) : valid;
        ASSERT_EQ(FeedUntilLastByte(input), recovering
                                                ? XGL_PARSE_RESULT_ERROR
                                                : XGL_PARSE_RESULT_COMPLETE);
        const auto saved_state = parser.state;
        const size_t saved_length = parser.cache_len;
        const std::vector<uint8_t> saved(cache.data(),
                                         cache.data() + saved_length);
        EXPECT_EQ(xgl_parser_feed_byte_view(&parser, 0x33U, 1U, &view, &status),
                  XGL_PARSE_RESULT_ERROR);
        EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
        EXPECT_EQ(parser.state, saved_state);
        EXPECT_EQ(parser.cache_len, saved_length);
        EXPECT_TRUE(std::equal(saved.begin(), saved.end(), cache.data()));
        if (recovering) {
            ASSERT_EQ(xgl_parser_advance_view(&parser, &view, &status),
                      XGL_PARSE_RESULT_COMPLETE);
        }
        ExpectCurrentFrame(valid);
        EXPECT_EQ(xgl_parser_advance_view(&parser, &view, &status),
                  XGL_PARSE_RESULT_INCOMPLETE);
    }
}

TEST_F(XglParserResyncTest, ValidPayloadContainingAFrameNeverTriggersRecovery) {
    const auto inner = make_frame({0x71U});
    Feed(make_frame(inner));
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({inner}));
}

TEST_F(XglParserResyncTest,
       SelectedPartialCandidateDoesNotPreemptAFrameInsideItsLaterPayload) {
    const auto inner = make_frame({0x71U});
    std::vector<uint8_t> payload = inner;
    payload.resize(payload.size() + 20U, 0x11U);
    const auto selected = make_frame(payload);
    constexpr size_t prefix = 8U;
    std::vector<uint8_t> contents(3U, 0x33U);
    contents.insert(contents.end(), selected.data(), selected.data() + prefix);
    Feed(rejected_envelope(contents));
    ASSERT_TRUE(delivered.empty());
    ASSERT_EQ(xgl_parser_get_cached_len(&parser), prefix);
    Feed(std::vector<uint8_t>(selected.data() + prefix,
                              selected.data() + selected.size() - 1U),
         1U);
    EXPECT_TRUE(delivered.empty());
    Feed({selected.back()}, 2U);
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({payload}));
    EXPECT_EQ(errors, std::vector<xgl_wire_decode_status_t>(
                          {XGL_WIRE_DECODE_FRAME_CRC}));
}

TEST_F(XglParserResyncTest,
       RecoveryRebasesExtensionPayloadAndFrameViewsWithSecurityMetadata) {
    const auto valid = make_frame_with_security({0x31U, 0x32U, 0x33U});
    const auto trailing = make_frame({0x44U});
    auto contents = valid;
    contents.insert(contents.end(), trailing.begin(), trailing.end());
    ASSERT_EQ(FeedUntilLastByte(rejected_envelope(contents)),
              XGL_PARSE_RESULT_ERROR);
    ASSERT_EQ(xgl_parser_advance_view(&parser, &view, &status),
              XGL_PARSE_RESULT_COMPLETE);
    ExpectCurrentFrame(valid);
    EXPECT_EQ(view.extensions, cache.data() + XGL_WIRE_BASE_HEADER_SIZE);
    EXPECT_EQ(view.extensions_len, 3U + XGL_SECURITY_EXT_SIZE);
    EXPECT_EQ(view.payload,
              cache.data() + XGL_WIRE_BASE_HEADER_SIZE + view.extensions_len);
    EXPECT_EQ(view.payload_len, 3U);
    EXPECT_EQ(view.payload[0], 0x31U);
    EXPECT_EQ(view.payload[2], 0x33U);
    EXPECT_EQ(view.data_type, 7U);
    EXPECT_TRUE(view.has_security_ext);
    EXPECT_TRUE(view.authenticated);
    EXPECT_EQ(view.auth_key_id, 0x12345678U);
    EXPECT_EQ(view.nonce_id, UINT64_C(0x0102030405060708));
    EXPECT_EQ(view.auth_tag_len, 16U);
    EXPECT_EQ(view.payload[view.payload_len], 0x6CU);
    EXPECT_GT(xgl_parser_get_cached_len(&parser), view.frame_len);
    ASSERT_EQ(xgl_parser_advance_view(&parser, &view, &status),
              XGL_PARSE_RESULT_COMPLETE);
    ExpectCurrentFrame(trailing);
    EXPECT_EQ(xgl_parser_advance_view(&parser, &view, &status),
              XGL_PARSE_RESULT_INCOMPLETE);
}

TEST_F(XglParserResyncTest,
       RecoveredPartialExtensionsResumeBeforePayloadAndTagValidation) {
    const auto valid = make_frame_with_security({0x51U, 0x52U});
    constexpr size_t partial_length = XGL_WIRE_BASE_HEADER_SIZE + 8U;
    Feed(rejected_envelope(
        std::vector<uint8_t>(valid.data(), valid.data() + partial_length)));
    ASSERT_TRUE(delivered.empty());
    ASSERT_EQ(parser.state, XGL_PARSE_EXTENSIONS);
    ASSERT_EQ(xgl_parser_get_cached_len(&parser), partial_length);
    ASSERT_EQ(FeedUntilLastByte(std::vector<uint8_t>(
                  valid.data() + partial_length, valid.data() + valid.size())),
              XGL_PARSE_RESULT_COMPLETE);
    ExpectCurrentFrame(valid);
    EXPECT_EQ(view.extensions, cache.data() + XGL_WIRE_BASE_HEADER_SIZE);
    EXPECT_EQ(view.payload, cache.data() + view.header.header_len);
    EXPECT_EQ(view.data_type, 7U);
    EXPECT_EQ(view.auth_tag_len, 16U);
    EXPECT_EQ(view.auth_key_id, 0x12345678U);
    EXPECT_EQ(view.nonce_id, UINT64_C(0x0102030405060708));
    EXPECT_EQ(errors, std::vector<xgl_wire_decode_status_t>(
                          {XGL_WIRE_DECODE_FRAME_CRC}));
}

TEST_F(XglParserResyncTest,
       RecoveryRejectsImpossibleTagCapacityBeforeRetainingTheNextPrefix) {
    auto impossible = make_frame_with_security(std::vector<uint8_t>(25U));
    const size_t header_length =
        XGL_WIRE_BASE_HEADER_SIZE + 3U + XGL_SECURITY_EXT_SIZE;
    impossible.resize(header_length);
    const auto valid = make_frame({0x61U});
    constexpr size_t partial_length = 13U;
    impossible.insert(impossible.end(), valid.data(),
                      valid.data() + partial_length);
    const auto envelope = rejected_envelope(impossible);
    ASSERT_EQ(envelope.size(), 79U);
    ASSERT_EQ(xgl_parser_init(&parser, cache.data(), envelope.size()), XGL_OK);
    Feed(envelope);
    ASSERT_TRUE(delivered.empty());
    ASSERT_EQ(xgl_parser_get_cached_len(&parser), partial_length);
    EXPECT_TRUE(
        std::equal(valid.data(), valid.data() + partial_length, cache.data()));
    Feed(std::vector<uint8_t>(valid.data() + partial_length,
                              valid.data() + valid.size()));
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0x61U}}));
    EXPECT_EQ(errors, std::vector<xgl_wire_decode_status_t>(
                          {XGL_WIRE_DECODE_FRAME_CRC}));
}

} /* namespace */
