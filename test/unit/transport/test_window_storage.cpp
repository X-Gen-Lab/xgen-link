/**
 * \file            test_window_storage.cpp
 * \brief           Compact ACK storage and repeated window reuse contracts
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "transport/xgl_window.h"

#if XGL_BUILD_WINDOW_CAPACITY >= 128
TEST(CompactWindowTest, AllocatesOnlyRequiredBitmapBytes) {
    const std::array<std::array<uint8_t, 2>, 6> cases = {
        {{1U, 1U}, {7U, 1U}, {8U, 1U}, {9U, 2U}, {127U, 16U}, {128U, 16U}}};
    for (const auto& item : cases) {
        SCOPED_TRACE(static_cast<unsigned>(item[0]));
        xgl_sliding_window_t window = {};
        ASSERT_EQ(xgl_window_init(&window, item[0]), XGL_OK);
        EXPECT_EQ(sizeof(window.ack_received), 16U);
        for (uint32_t index = 0U; index < item[0]; ++index) {
            ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, index),
                      XGL_OK);
        }
        EXPECT_EQ(xgl_window_advance_base_packet_number(&window), item[0]);
        xgl_window_destroy(&window);
        xgl_window_destroy(&window);
    }
}
#endif

TEST(CompactWindowTest, FailedInitializationPreservesCallerState) {
    xgl_sliding_window_t window = {};
    window.window_size = 9U;
    window.ack_head = 4U;
    window.send_base_packet_number = 123U;
    window.next_packet_number = 127U;
    const auto before = window;
    EXPECT_EQ(xgl_window_init(&window, 129U), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(window.window_size, before.window_size);
    EXPECT_EQ(window.ack_head, before.ack_head);
    EXPECT_EQ(window.send_base_packet_number, before.send_base_packet_number);
    EXPECT_EQ(window.next_packet_number, before.next_packet_number);
    EXPECT_EQ(std::memcmp(window.ack_received, before.ack_received,
                          sizeof(window.ack_received)),
              0);
}

#if XGL_BUILD_WINDOW_CAPACITY >= 9
TEST(CompactWindowTest, ReusesNonByteAlignedWindowWithoutStaleAcknowledgments) {
    xgl_sliding_window_t window = {};
    ASSERT_EQ(xgl_window_init(&window, 9U), XGL_OK);
    for (uint32_t cycle = 0U; cycle < 40U; ++cycle) {
        const uint32_t base = cycle * 9U;
        for (uint32_t offset = 0U; offset < 9U; ++offset) {
            xgl_window_advance_next_packet_number(&window);
        }
        for (uint32_t offset = 1U; offset < 9U; offset += 2U) {
            ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, base + offset),
                      XGL_OK);
        }
        EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 0U);
        for (uint32_t offset = 0U; offset < 9U; offset += 2U) {
            ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, base + offset),
                      XGL_OK);
            EXPECT_EQ(xgl_window_advance_base_packet_number(&window),
                      offset == 8U ? 1U : 2U);
        }
        EXPECT_EQ(window.send_base_packet_number, base + 9U);
        EXPECT_EQ(xgl_window_get_usage(&window), 0U);
        EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 0U);
    }
    xgl_window_destroy(&window);
}

TEST(CompactWindowTest, ResetClearsRotatedAcknowledgments) {
    xgl_sliding_window_t window = {};
    ASSERT_EQ(xgl_window_init(&window, 9U), XGL_OK);
    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 0U), XGL_OK);
    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 8U), XGL_OK);
    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 1U);
    xgl_window_reset(&window);
    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 0U);
    for (uint32_t offset = 0U; offset < 8U; ++offset) {
        ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, offset), XGL_OK);
    }
    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 8U);
    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 0U);
    xgl_window_destroy(&window);
}

TEST(CompactWindowTest, AcknowledgesNewTailAfterPartialAdvanceAtMaximumWidths) {
    for (uint8_t width : std::array<uint8_t, 3>{9U, 127U, 128U}) {
        SCOPED_TRACE(static_cast<unsigned>(width));
        xgl_sliding_window_t window = {};
        ASSERT_EQ(xgl_window_init(&window, width), XGL_OK);
        for (uint32_t packet = 0U; packet < width; ++packet) {
            ASSERT_TRUE(xgl_window_can_send_packet_number(&window));
            xgl_window_advance_next_packet_number(&window);
        }
        ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 0U), XGL_OK);
        ASSERT_EQ(xgl_window_advance_base_packet_number(&window), 1U);
        ASSERT_EQ(window.ack_head, 1U);

        /* The newly available tail reuses the physical slot before the head. */
        ASSERT_TRUE(xgl_window_can_send_packet_number(&window));
        xgl_window_advance_next_packet_number(&window);
        ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, width), XGL_OK);
        EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 0U);
        for (uint32_t packet = 1U; packet < width; ++packet) {
            ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, packet),
                      XGL_OK);
        }
        EXPECT_EQ(xgl_window_advance_base_packet_number(&window), width);
        EXPECT_EQ(window.send_base_packet_number,
                  static_cast<uint32_t>(width) + 1U);
        EXPECT_EQ(xgl_window_get_usage(&window), 0U);
        EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 0U);

        /* Refill a complete rotated window and prove no stale ACK survives. */
        const uint32_t base = window.send_base_packet_number;
        for (uint32_t offset = 0U; offset < width; ++offset) {
            xgl_window_advance_next_packet_number(&window);
        }
        EXPECT_FALSE(xgl_window_can_send_packet_number(&window));
        EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 0U);
        for (uint32_t offset = 0U; offset < width; ++offset) {
            ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, base + offset),
                      XGL_OK);
        }
        EXPECT_EQ(xgl_window_advance_base_packet_number(&window), width);
        EXPECT_EQ(window.send_base_packet_number, base + width);
        EXPECT_EQ(xgl_window_get_usage(&window), 0U);
        xgl_window_destroy(&window);
    }
}
#endif
