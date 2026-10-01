/**
 * \file            test_datalink_span.cpp
 * \brief           Synchronous byte-span framing and polling contracts
 */

#include <datalink/xgl_datalink.h>
#include <wire/xgl_frame.h>
#include <wire/xgl_wire.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

class XglDatalinkSpanTest : public ::testing::Test {
  protected:
    xgl_datalink_ctx_t context = {};
    xgl_parser_t parser = {};
    xgl_layer_stats_t statistics = {};
    xgl_frame_rx_endpoint_t upstream = {};
    std::array<uint8_t, 256> cache = {};
    std::vector<std::vector<uint8_t>> delivered;
    std::vector<xgl_error_t> replies;
    std::vector<xgl_error_t> errors;
    std::vector<uint8_t> driver_bytes;
    size_t driver_cursor = 0U;
    size_t reads = 0U;
    uint64_t header_crc_errors = 0U;
    uint64_t frame_crc_errors = 0U;
    xgl_phy_ops_t phy = {};

    void SetUp() override {
        upstream.ctx = this;
        upstream.receive = [](void* user, xgl_handle_t,
                              const xgl_frame_rx_message_t* message) {
            auto& self = *static_cast<XglDatalinkSpanTest*>(user);
            const auto* view = message->view;
            EXPECT_NE(view, nullptr);
            if (view == nullptr) {
                return XGL_ERR_INVALID_FRAME;
            }
            EXPECT_EQ(view->frame_buf, self.cache.data());
            EXPECT_LE(view->frame_len, self.cache.size());
            const size_t index = self.delivered.size();
            self.delivered.emplace_back(view->payload,
                                        view->payload + view->payload_len);
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
            static_cast<XglDatalinkSpanTest*>(user)->errors.push_back(error);
        };
        ASSERT_EQ(xgl_datalink_init(&context, &config), XGL_OK);
        ASSERT_EQ(xgl_parser_init(&parser, cache.data(), cache.size()), XGL_OK);
        phy.user_data = this;
        phy.rx = [](uint8_t* bytes, size_t* size, void* user) {
            auto& self = *static_cast<XglDatalinkSpanTest*>(user);
            ++self.reads;
            *size =
                std::min(*size, self.driver_bytes.size() - self.driver_cursor);
            if (*size > 0U) {
                std::memcpy(bytes,
                            self.driver_bytes.data() + self.driver_cursor,
                            *size);
            }
            self.driver_cursor += *size;
            return XGL_OK;
        };
    }

    static std::vector<uint8_t> frame(uint8_t payload) {
        const uint8_t extensions[] = {
            XGL_WIRE_EXT_DATA_TYPE, 1U, 7U, 250U, 2U, 0x12U, 0x34U};
        xgl_frame_params_t params = {};
        params.source_id = 1U;
        params.target_id = 2U;
        params.extensions = extensions;
        params.extensions_len = sizeof(extensions);
        params.payload = &payload;
        params.payload_len = 1U;
        xgl_frame_t logical = {};
        EXPECT_EQ(xgl_frame_build(&logical, &params), XGL_OK);
        std::vector<uint8_t> bytes(64U);
        size_t size = 0U;
        EXPECT_EQ(
            xgl_frame_serialize(bytes.data(), bytes.size(), &logical, &size),
            XGL_OK);
        bytes.resize(size);
        return bytes;
    }

    xgl_error_t consume(const std::vector<uint8_t>& bytes,
                        uint32_t now_ms = 10U) {
        return xgl_datalink_consume_span(&context, &parser, bytes.data(),
                                         bytes.size(), now_ms);
    }
};

TEST_F(XglDatalinkSpanTest, EmptySpanAndInvalidPointersPreservePartialFrame) {
    const auto bytes = frame(7U);
    ASSERT_EQ(
        xgl_datalink_consume_span(&context, &parser, bytes.data(), 5U, 1U),
        XGL_OK);
    const size_t size = parser.cache_len;
    EXPECT_EQ(xgl_datalink_consume_span(nullptr, &parser, bytes.data(), 1U, 2U),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(
        xgl_datalink_consume_span(&context, nullptr, bytes.data(), 1U, 2U),
        XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_consume_span(&context, &parser, nullptr, 1U, 2U),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_datalink_consume_span(&context, &parser, nullptr, 0U, 999U),
              XGL_OK);
    EXPECT_EQ(parser.cache_len, size);
    EXPECT_EQ(parser.timestamp, 1U);
    EXPECT_TRUE(errors.empty());
    EXPECT_TRUE(delivered.empty());
}

TEST_F(XglDatalinkSpanTest, EverySplitPointPreservesHeadersExtensionsAndCrc) {
    const auto bytes = frame(0x42U);
    for (size_t split = 0U; split <= bytes.size(); ++split) {
        SCOPED_TRACE(split);
        delivered.clear();
        xgl_parser_reset(&parser);
        ASSERT_EQ(xgl_datalink_consume_span(&context, &parser, bytes.data(),
                                            split, 1U),
                  XGL_OK);
        ASSERT_EQ(xgl_datalink_consume_span(&context, &parser,
                                            bytes.data() + split,
                                            bytes.size() - split, 2U),
                  XGL_OK);
        ASSERT_EQ(delivered.size(), 1U);
        EXPECT_EQ(delivered[0], std::vector<uint8_t>({0x42U}));
        EXPECT_EQ(parser.state, XGL_PARSE_MAGIC);
    }
}

TEST_F(XglDatalinkSpanTest, MultipleFramesRetainOnlyTheTrailingPartialFrame) {
    const auto first = frame(1U);
    const auto second = frame(2U);
    const auto third = frame(3U);
    std::vector<uint8_t> bytes = first;
    bytes.insert(bytes.end(), second.begin(), second.end());
    bytes.insert(bytes.end(), third.begin(), third.begin() + 13);
    for (size_t width = 1U; width <= bytes.size(); ++width) {
        SCOPED_TRACE(width);
        delivered.clear();
        xgl_parser_reset(&parser);
        for (size_t offset = 0U; offset < bytes.size(); offset += width) {
            ASSERT_EQ(xgl_datalink_consume_span(
                          &context, &parser, bytes.data() + offset,
                          std::min(width, bytes.size() - offset), 10U),
                      XGL_OK);
        }
        ASSERT_EQ(delivered.size(), 2U);
        EXPECT_EQ(parser.cache_len, 13U);
        ASSERT_EQ(xgl_datalink_consume_span(&context, &parser,
                                            third.data() + 13U,
                                            third.size() - 13U, 11U),
                  XGL_OK);
        ASSERT_EQ(delivered.size(), 3U);
        EXPECT_EQ(delivered[0], std::vector<uint8_t>({1U}));
        EXPECT_EQ(delivered[1], std::vector<uint8_t>({2U}));
        EXPECT_EQ(delivered[2], std::vector<uint8_t>({3U}));
    }
}

TEST_F(XglDatalinkSpanTest, CorruptFrameDoesNotDiscardTheFollowingValidFrame) {
    auto corrupt = frame(1U);
    corrupt.back() ^= 1U;
    const auto good = frame(2U);
    corrupt.insert(corrupt.end(), good.begin(), good.end());
    EXPECT_EQ(consume(corrupt), XGL_OK);
    ASSERT_EQ(delivered.size(), 1U);
    EXPECT_EQ(delivered[0], std::vector<uint8_t>({2U}));
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_CRC_FAILED);
#if XGL_FEATURE_STATISTICS
    EXPECT_EQ(statistics.rx_errors, 1U);
    EXPECT_EQ(statistics.rx_packets, 1U);
    EXPECT_EQ(header_crc_errors, 0U);
    EXPECT_EQ(frame_crc_errors, 1U);
#endif
}

TEST_F(XglDatalinkSpanTest, BusyDeliveryStillConsumesFollowingFramesAndTail) {
    const auto first = frame(1U);
    const auto second = frame(2U);
    const auto third = frame(3U);
    std::vector<uint8_t> bytes = first;
    bytes.insert(bytes.end(), second.begin(), second.end());
    bytes.insert(bytes.end(), third.begin(), third.begin() + 9);
    replies = {XGL_ERR_BUSY, XGL_ERR_INVALID_PARAM, XGL_OK};
    EXPECT_EQ(consume(bytes), XGL_ERR_BUSY);
    EXPECT_EQ(delivered.size(), 2U);
    EXPECT_EQ(parser.cache_len, 9U);
    EXPECT_EQ(xgl_datalink_consume_span(&context, &parser, third.data() + 9U,
                                        third.size() - 9U, 20U),
              XGL_OK);
    ASSERT_EQ(delivered.size(), 3U);
    EXPECT_EQ(delivered[2], std::vector<uint8_t>({3U}));
    EXPECT_TRUE(errors.empty());
}

TEST_F(XglDatalinkSpanTest, PartialFrameDoesNotRetainTheBorrowedInputSpan) {
    const auto complete = frame(0x42U);
    auto first = complete;
    first.resize(12U);
    ASSERT_EQ(consume(first), XGL_OK);
    std::fill(first.begin(), first.end(), 0xFFU);
    ASSERT_EQ(xgl_datalink_consume_span(&context, &parser,
                                        complete.data() + 12U,
                                        complete.size() - 12U, 20U),
              XGL_OK);
    ASSERT_EQ(delivered.size(), 1U);
    EXPECT_EQ(delivered[0], std::vector<uint8_t>({0x42U}));
}

TEST_F(XglDatalinkSpanTest, PollCallsThePhyOnlyOncePerInvocation) {
    const auto first = frame(1U);
    driver_bytes = first;
    driver_bytes.insert(driver_bytes.end(), first.begin(), first.end());
    do {
        const size_t before = reads;
        ASSERT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 1U, 100U,
                                           driver_bytes.size()),
                  XGL_OK);
        EXPECT_EQ(reads, before + 1U);
    } while (driver_cursor < driver_bytes.size());
    EXPECT_EQ(delivered.size(), 2U);
}

TEST_F(XglDatalinkSpanTest, PollRetainsTimeoutWhileConsumingFreshInput) {
    const auto bytes = frame(7U);
    ASSERT_EQ(
        xgl_datalink_consume_span(&context, &parser, bytes.data(), 5U, 1U),
        XGL_OK);
    driver_bytes = bytes;
    replies = {XGL_ERR_BUSY};
    EXPECT_EQ(xgl_datalink_poll_parser(&context, &parser, &phy, 20U, 10U,
                                       driver_bytes.size()),
              XGL_ERR_TIMEOUT);
    while (driver_cursor < driver_bytes.size()) {
        const auto result = xgl_datalink_poll_parser(
            &context, &parser, &phy, 20U, 10U, driver_bytes.size());
        EXPECT_EQ(result,
                  driver_cursor == driver_bytes.size() ? XGL_ERR_BUSY : XGL_OK);
    }
    EXPECT_EQ(delivered.size(), 1U);
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0], XGL_ERR_TIMEOUT);
}
