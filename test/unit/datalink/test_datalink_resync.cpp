/**
 * \file            test_datalink_resync.cpp
 * \brief           Cached receive recovery and complete-frame consumption
 * \author          X-Gen Lab
 */

#include <datalink/xgl_datalink.h>
#include <wire/xgl_frame.h>

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <vector>

namespace {

class XglDatalinkResyncTest : public ::testing::Test {
  protected:
    void SetUp() override {
        upstream.ctx = this;
        upstream.receive = [](void* user, xgl_handle_t,
                              const xgl_frame_rx_message_t* message) {
            auto& self = *static_cast<XglDatalinkResyncTest*>(user);
            if (message->view == nullptr) {
                ADD_FAILURE() << "Receive must borrow a validated view";
                return XGL_ERR_INVALID_FRAME;
            }
            const auto& view = *message->view;
            EXPECT_EQ(message->frame_buf, view.frame_buf);
            EXPECT_EQ(message->frame_len, view.frame_len);
            const size_t index = self.delivered.size();
            self.delivered.emplace_back(view.payload,
                                        view.payload + view.payload_len);
            self.authenticated.push_back(view.authenticated);
            return index < self.replies.size() ? self.replies[index] : XGL_OK;
        };
        xgl_datalink_config_t config = {};
        config.stats = &statistics;
        config.rx_header_crc_errors = &header_crc_errors;
        config.rx_crc16_errors = &frame_crc_errors;
        config.upper_layer = &upstream;
        config.callback_user_data = this;
        config.error_callback = [](xgl_handle_t, xgl_error_t error, const char*,
                                   void* user) {
            static_cast<XglDatalinkResyncTest*>(user)->errors.push_back(error);
        };
        ASSERT_EQ(xgl_datalink_init(&context, &config), XGL_OK);
        ASSERT_EQ(xgl_parser_init(&parser, cache.data(), cache.size()), XGL_OK);
    }

    static std::vector<uint8_t> Frame(const std::vector<uint8_t>& payload,
                                      bool authenticated = false) {
        xgl_frame_params_t params = {};
        params.source_id = 1U;
        params.target_id = 2U;
        params.payload = payload.data();
        params.payload_len = payload.size();
        xgl_frame_t frame = {};
        EXPECT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
        std::vector<uint8_t> bytes(payload.size() + 64U);
        xgl_frame_layout_t layout = {};
        EXPECT_EQ(xgl_frame_encode_into(bytes.data(), bytes.size(), &frame,
                                        authenticated ? 4U : 0U, &layout),
                  XGL_OK);
        size_t written = 0U;
        EXPECT_EQ(xgl_frame_finalize_crc(bytes.data(), bytes.size(), &layout,
                                         &written),
                  XGL_OK);
        bytes.resize(written);
        return bytes;
    }

    static std::vector<uint8_t> CorruptOuter() {
        auto payload = Frame({0xB0U});
        const auto second = Frame({0xC0U});
        payload.insert(payload.end(), second.begin(), second.end());
        auto bytes = Frame(payload);
        bytes.back() ^= 1U;
        return bytes;
    }

    xgl_error_t Consume(const std::vector<uint8_t>& bytes) {
        return xgl_datalink_consume_span(&context, &parser, bytes.data(),
                                         bytes.size(), 10U);
    }

    void ExpectOneCrcFailure(size_t packet_count) {
        EXPECT_EQ(errors, std::vector<xgl_error_t>({XGL_ERR_CRC_FAILED}));
#if XGL_FEATURE_STATISTICS
        EXPECT_EQ(statistics.rx_errors, 1U);
        EXPECT_EQ(statistics.rx_packets, packet_count);
        EXPECT_EQ(header_crc_errors, 0U);
        EXPECT_EQ(frame_crc_errors, 1U);
#else
        (void)packet_count;
#endif
    }

    xgl_datalink_ctx_t context = {};
    xgl_parser_t parser = {};
    xgl_layer_stats_t statistics = {};
    xgl_frame_rx_endpoint_t upstream = {};
    std::array<uint8_t, 256> cache = {};
    std::vector<std::vector<uint8_t>> delivered;
    std::vector<bool> authenticated;
    std::vector<xgl_error_t> replies;
    std::vector<xgl_error_t> errors;
    uint64_t header_crc_errors = 0U;
    uint64_t frame_crc_errors = 0U;
};

TEST_F(XglDatalinkResyncTest,
       CorruptOuterDeliversCachedFramesBeforeFollowingInputAtEverySplit) {
    auto bytes = CorruptOuter();
    const auto next = Frame({0xD0U});
    bytes.insert(bytes.end(), next.begin(), next.end());
    for (size_t split = 0U; split <= bytes.size(); ++split) {
        SCOPED_TRACE(split);
        xgl_parser_reset(&parser);
        delivered.clear();
        errors.clear();
        statistics = {};
        header_crc_errors = 0U;
        frame_crc_errors = 0U;
        ASSERT_EQ(xgl_datalink_consume_span(&context, &parser, bytes.data(),
                                            split, 1U),
                  XGL_OK);
        ASSERT_EQ(xgl_datalink_consume_span(&context, &parser,
                                            bytes.data() + split,
                                            bytes.size() - split, 2U),
                  XGL_OK);
        EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>(
                                 {{0xB0U}, {0xC0U}, {0xD0U}}));
        ExpectOneCrcFailure(3U);
        EXPECT_EQ(parser.cache_len, 0U);
    }
}

TEST_F(XglDatalinkResyncTest,
       CachedBusyPreservesFirstErrorAndDrainsFollowingFrames) {
    auto bytes = CorruptOuter();
    const auto next = Frame({0xD0U});
    bytes.insert(bytes.end(), next.begin(), next.end());
    replies = {XGL_ERR_BUSY, XGL_ERR_INVALID_FRAME, XGL_OK};
    EXPECT_EQ(Consume(bytes), XGL_ERR_BUSY);
    EXPECT_EQ(delivered,
              std::vector<std::vector<uint8_t>>({{0xB0U}, {0xC0U}, {0xD0U}}));
    ExpectOneCrcFailure(3U);
    EXPECT_EQ(parser.cache_len, 0U);
}

TEST_F(XglDatalinkResyncTest, WireValidBusyDoesNotScanPayload) {
    const auto payload = Frame({0xB0U});
    auto bytes = Frame(payload);
    const auto next = Frame({0xD0U});
    bytes.insert(bytes.end(), next.begin(), next.end());
    replies = {XGL_ERR_BUSY, XGL_OK};
    EXPECT_EQ(Consume(bytes), XGL_ERR_BUSY);
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({payload, {0xD0U}}));
    EXPECT_TRUE(errors.empty());
#if XGL_FEATURE_STATISTICS
    EXPECT_EQ(statistics.rx_errors, 0U);
    EXPECT_EQ(statistics.rx_packets, 2U);
#endif
}

TEST_F(XglDatalinkResyncTest,
       WireValidAuthenticationRejectionDoesNotScanPayload) {
    const auto payload = Frame({0xB0U});
    auto bytes = Frame(payload, true);
    const auto next = Frame({0xD0U});
    bytes.insert(bytes.end(), next.begin(), next.end());
    replies = {XGL_ERR_INVALID_FRAME, XGL_OK};
    EXPECT_EQ(Consume(bytes), XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({payload, {0xD0U}}));
    EXPECT_EQ(authenticated, std::vector<bool>({true, false}));
    EXPECT_TRUE(errors.empty());
#if XGL_FEATURE_STATISTICS
    EXPECT_EQ(statistics.rx_errors, 0U);
    EXPECT_EQ(statistics.rx_packets, 2U);
#endif
}

TEST_F(XglDatalinkResyncTest,
       EmptySpanDrainsPendingRecoveryWithoutRepeatingDelivery) {
    const auto bytes = CorruptOuter();
    xgl_wire_frame_view_t view = {};
    xgl_wire_decode_status_t status = XGL_WIRE_DECODE_INVALID;
    for (size_t i = 0U; i < bytes.size(); ++i) {
        ASSERT_EQ(
            xgl_parser_feed_byte_view(&parser, bytes[i], 1U, &view, &status),
            i + 1U == bytes.size() ? XGL_PARSE_RESULT_ERROR
                                   : XGL_PARSE_RESULT_INCOMPLETE);
    }
    ASSERT_EQ(status, XGL_WIRE_DECODE_FRAME_CRC);
    EXPECT_EQ(xgl_datalink_consume_span(&context, &parser, nullptr, 0U, 2U),
              XGL_OK);
    EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0xB0U}, {0xC0U}}));
    EXPECT_EQ(xgl_datalink_consume_span(&context, &parser, nullptr, 0U, 3U),
              XGL_OK);
    EXPECT_EQ(delivered.size(), 2U);
    EXPECT_TRUE(errors.empty());
    EXPECT_EQ(parser.cache_len, 0U);
}

TEST_F(XglDatalinkResyncTest, TruncatedCrcRecoversTheFollowingFrame) {
    for (size_t missing = 1U; missing <= XGL_CRC16_SIZE; ++missing) {
        SCOPED_TRACE(missing);
        xgl_parser_reset(&parser);
        delivered.clear();
        errors.clear();
        statistics = {};
        header_crc_errors = 0U;
        frame_crc_errors = 0U;
        auto bytes = Frame({0xA0U});
        bytes.resize(bytes.size() - missing);
        const auto next = Frame({0xB0U});
        bytes.insert(bytes.end(), next.begin(), next.end());
        EXPECT_EQ(Consume(bytes), XGL_OK);
        EXPECT_EQ(delivered, std::vector<std::vector<uint8_t>>({{0xB0U}}));
        ExpectOneCrcFailure(1U);
        EXPECT_EQ(parser.cache_len, 0U);
    }
}

}  // namespace
