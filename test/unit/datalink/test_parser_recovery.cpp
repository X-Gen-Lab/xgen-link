/**
 * \file            test_parser_recovery.cpp
 * \brief           Parser deadlines, bounded admission and view contracts
 * \author          X-Gen Lab
 */

#include <datalink/xgl_parser.h>
#include <wire/xgl_wire.h>

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <vector>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

namespace {

std::array<uint8_t, XGL_WIRE_BASE_HEADER_SIZE>
make_header(uint8_t header_length, uint16_t payload_length,
            uint8_t flags = 0U) {
    std::array<uint8_t, XGL_WIRE_BASE_HEADER_SIZE> bytes = {};
    xgl_wire_header_t header = {};
    header.version = XGL_WIRE_VERSION;
    header.header_len = header_length;
    header.packet_type = XGL_PACKET_TYPE_DATA;
    header.flags = flags;
    header.source_id = 1U;
    header.target_id = 2U;
    header.payload_len = payload_length;
    EXPECT_EQ(xgl_wire_encode_header(bytes.data(), bytes.size(), &header),
              XGL_OK);
    return bytes;
}

std::vector<uint8_t> make_frame(uint16_t payload_length = 2U) {
    const auto header = make_header(XGL_WIRE_BASE_HEADER_SIZE, payload_length);
    std::vector<uint8_t> bytes(header.size() + payload_length + XGL_CRC16_SIZE,
                               0x42U);
    std::copy(header.begin(), header.end(), bytes.begin());
    const size_t body_end = bytes.size() - XGL_CRC16_SIZE;
    xgb_serialize_u16_le(bytes.data() + body_end,
                         xgcrc_crc16_modbus(bytes.data(), body_end));
    return bytes;
}

class XglParserRecoveryTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_EQ(xgl_parser_init(&parser, cache.data(), cache.size()), XGL_OK);
    }

    void ExpectOverlapDeadline(uint32_t started) {
        const auto frame = make_frame();
        const uint32_t replaced = started + 9U;
        ASSERT_EQ(xgl_parser_feed_byte_view(&parser, frame[0], started, &view,
                                            &status),
                  XGL_PARSE_RESULT_INCOMPLETE);
        ASSERT_EQ(xgl_parser_feed_byte_view(&parser, frame[0], replaced, &view,
                                            &status),
                  XGL_PARSE_RESULT_INCOMPLETE);
        EXPECT_FALSE(xgl_parser_check_timeout(&parser, replaced + 1U, 10U));
        EXPECT_FALSE(xgl_parser_check_timeout(&parser, replaced + 9U, 10U));
        EXPECT_TRUE(xgl_parser_check_timeout(&parser, replaced + 10U, 10U));

        for (size_t i = 1U; i < frame.size(); ++i) {
            EXPECT_EQ(xgl_parser_feed_byte_view(&parser, frame[i],
                                                replaced + 1U, &view, &status),
                      i + 1U == frame.size() ? XGL_PARSE_RESULT_COMPLETE
                                             : XGL_PARSE_RESULT_INCOMPLETE);
        }
        EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
        ASSERT_EQ(view.payload_len, 2U);
        EXPECT_EQ(view.payload[0], 0x42U);
    }

    std::array<uint8_t, 50> cache = {};
    xgl_parser_t parser = {};
    xgl_wire_frame_view_t view = {};
    xgl_wire_decode_status_t status = XGL_WIRE_DECODE_INVALID;
};

TEST_F(XglParserRecoveryTest, OverlappingMagicStartsANewCandidateDeadline) {
    ExpectOverlapDeadline(0U);
}

TEST_F(XglParserRecoveryTest, OverlappingMagicDeadlineSurvivesClockWrap) {
    ExpectOverlapDeadline(UINT32_MAX - 5U);
}

TEST_F(XglParserRecoveryTest, LaterBodyBytesDoNotExtendTheCandidateDeadline) {
    const auto frame = make_frame(8U);
    ASSERT_EQ(
        xgl_parser_feed_byte_view(&parser, frame[0], 100U, &view, &status),
        XGL_PARSE_RESULT_INCOMPLETE);
    for (size_t i = 1U; i <= XGL_WIRE_BASE_HEADER_SIZE; ++i) {
        ASSERT_EQ(
            xgl_parser_feed_byte_view(&parser, frame[i], 109U, &view, &status),
            XGL_PARSE_RESULT_INCOMPLETE);
    }
    EXPECT_FALSE(xgl_parser_check_timeout(&parser, 109U, 10U));
    EXPECT_TRUE(xgl_parser_check_timeout(&parser, 110U, 10U));
    EXPECT_FALSE(xgl_parser_check_timeout(&parser, 110U, 0U));
}

TEST_F(XglParserRecoveryTest,
       ImpossibleLengthsRejectAtBaseHeaderAndAcceptTheNextFrame) {
    const std::array<std::array<uint16_t, 2>, 4> lengths = {
        {{60U, 0U}, {49U, 0U}, {27U, 22U}, {255U, UINT16_MAX}}};
    const auto valid = make_frame();
    for (const auto& length : lengths) {
        SCOPED_TRACE(::testing::Message()
                     << "header=" << length[0] << " payload=" << length[1]);
        xgl_parser_reset(&parser);
        const auto header =
            make_header(static_cast<uint8_t>(length[0]), length[1]);
        for (size_t i = 0U; i < header.size(); ++i) {
            EXPECT_EQ(xgl_parser_feed_byte_view(&parser, header[i], 0U, &view,
                                                &status),
                      i + 1U == header.size() ? XGL_PARSE_RESULT_ERROR
                                              : XGL_PARSE_RESULT_INCOMPLETE);
        }
        EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
        EXPECT_EQ(xgl_parser_get_state(&parser), XGL_PARSE_MAGIC);
        EXPECT_EQ(xgl_parser_get_cached_len(&parser), 0U);

        for (size_t i = 0U; i < valid.size(); ++i) {
            EXPECT_EQ(xgl_parser_feed_byte_view(&parser, valid[i], 1U, &view,
                                                &status),
                      i + 1U == valid.size() ? XGL_PARSE_RESULT_COMPLETE
                                             : XGL_PARSE_RESULT_INCOMPLETE);
        }
        EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
        EXPECT_EQ(view.frame_buf, cache.data());
        EXPECT_EQ(view.frame_len, valid.size());
    }
}

TEST_F(XglParserRecoveryTest, ExactCapacityRequiresBothCrcBytes) {
    const auto frame = make_frame(24U);
    ASSERT_EQ(frame.size(), cache.size());
    for (size_t i = 0U; i < frame.size(); ++i) {
        ASSERT_EQ(
            xgl_parser_feed_byte_view(&parser, frame[i], 0U, &view, &status),
            i + 1U == frame.size() ? XGL_PARSE_RESULT_COMPLETE
                                   : XGL_PARSE_RESULT_INCOMPLETE);
    }
    EXPECT_EQ(view.frame_len, cache.size());
    EXPECT_EQ(view.payload_len, 24U);
    EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
}

TEST_F(XglParserRecoveryTest, AuthenticationTrailerIsBoundedAfterExtensions) {
    const auto header =
        make_header(XGL_WIRE_BASE_HEADER_SIZE + XGL_SECURITY_EXT_SIZE, 0U,
                    XGL_WIRE_FLAG_AUTHENTICATED | XGL_WIRE_FLAG_HAS_EXTENSIONS);
    std::array<uint8_t, XGL_SECURITY_EXT_SIZE> extension = {};
    extension[0] = XGL_WIRE_EXT_SECURITY;
    extension[1] = XGL_SECURITY_EXT_VALUE_SIZE;
    size_t written = 0U;
    ASSERT_EQ(xgl_wire_encode_security_ext_value(
                  extension.data() + XGL_WIRE_EXT_HEADER_SIZE,
                  extension.size() - XGL_WIRE_EXT_HEADER_SIZE, 1U, 2U, 20U,
                  &written),
              XGL_OK);
    for (const auto byte : header) {
        ASSERT_EQ(xgl_parser_feed_byte_view(&parser, byte, 0U, &view, &status),
                  XGL_PARSE_RESULT_INCOMPLETE);
    }
    for (size_t i = 0U; i < extension.size(); ++i) {
        EXPECT_EQ(xgl_parser_feed_byte_view(&parser, extension[i], 0U, &view,
                                            &status),
                  i + 1U == extension.size() ? XGL_PARSE_RESULT_ERROR
                                             : XGL_PARSE_RESULT_INCOMPLETE);
    }
    EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
    EXPECT_EQ(xgl_parser_get_state(&parser), XGL_PARSE_MAGIC);
}

TEST_F(XglParserRecoveryTest, MissingViewRejectsWithoutConsumingInput) {
    const auto frame = make_frame();
    ASSERT_EQ(xgl_parser_feed_byte_view(&parser, frame[0], 0U, &view, &status),
              XGL_PARSE_RESULT_INCOMPLETE);
    EXPECT_EQ(
        xgl_parser_feed_byte_view(&parser, frame[1], 1U, nullptr, &status),
        XGL_PARSE_RESULT_ERROR);
    EXPECT_EQ(status, XGL_WIRE_DECODE_INVALID);
    EXPECT_EQ(xgl_parser_get_cached_len(&parser), 1U);
    for (size_t i = 1U; i < frame.size(); ++i) {
        EXPECT_EQ(
            xgl_parser_feed_byte_view(&parser, frame[i], 1U, &view, &status),
            i + 1U == frame.size() ? XGL_PARSE_RESULT_COMPLETE
                                   : XGL_PARSE_RESULT_INCOMPLETE);
    }
    EXPECT_EQ(status, XGL_WIRE_DECODE_OK);
}

TEST_F(XglParserRecoveryTest, LegacyByteFeedKeepsValidationAndFrameAccess) {
    const auto valid = make_frame();
    auto corrupt = valid;
    corrupt.back() ^= 1U;
    for (size_t i = 0U; i < corrupt.size(); ++i) {
        ASSERT_EQ(xgl_parser_feed_byte(&parser, corrupt[i], 0U),
                  i + 1U == corrupt.size() ? XGL_PARSE_RESULT_ERROR
                                           : XGL_PARSE_RESULT_INCOMPLETE);
    }
    for (size_t i = 0U; i < valid.size(); ++i) {
        ASSERT_EQ(xgl_parser_feed_byte(&parser, valid[i], 1U),
                  i + 1U == valid.size() ? XGL_PARSE_RESULT_COMPLETE
                                         : XGL_PARSE_RESULT_INCOMPLETE);
    }
    uint8_t* bytes = nullptr;
    size_t length = 0U;
    ASSERT_EQ(xgl_parser_get_frame(&parser, &bytes, &length), XGL_OK);
    EXPECT_EQ(bytes, cache.data());
    ASSERT_EQ(length, valid.size());
    EXPECT_TRUE(std::equal(valid.begin(), valid.end(), bytes));
}

}  // namespace
