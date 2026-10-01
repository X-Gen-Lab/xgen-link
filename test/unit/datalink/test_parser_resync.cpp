/**
 * \file            test_parser_resync.cpp
 * \brief           Buffered CRC recovery and frame-consumption contracts
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
    xgb_serialize_u16_le(bytes.data() + crc_offset,
                         xgcrc_crc16_modbus(bytes.data(), crc_offset));
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
    ASSERT_NE(xgb_deserialize_u16_le(truncated.data() + truncated.size() -
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

}  // namespace
