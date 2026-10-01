/**
 * \file            test_window_storage.cpp
 * \brief           Compact ACK storage and repeated window reuse contracts
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <xgen/memory/allocator.h>

#include "transport/xgl_window.h"

namespace {
struct AllocationProbe {
    size_t requested = 0U;
    size_t released = 0U;
    bool fail = false;
};

void* allocate(void* context, size_t size) {
    auto* probe = static_cast<AllocationProbe*>(context);
    probe->requested = size;
    return probe->fail ? nullptr : std::malloc(size);
}

void release(void* context, void* pointer) {
    auto* probe = static_cast<AllocationProbe*>(context);
    ++probe->released;
    std::free(pointer);
}
} /* namespace */

TEST(CompactWindowTest, AllocatesOnlyRequiredBitmapBytes) {
    const std::array<std::array<uint8_t, 2>, 6> cases = {
        {{1U, 1U}, {7U, 1U}, {8U, 1U}, {9U, 2U}, {127U, 16U}, {128U, 16U}}};
    for (const auto& item : cases) {
        SCOPED_TRACE(static_cast<unsigned>(item[0]));
        AllocationProbe probe;
        const xgm_allocator_t allocator = {&probe, allocate, release};
        xgl_sliding_window_t window = {};
        ASSERT_EQ(xgl_window_init_with_allocator(&window, item[0], &allocator),
                  XGL_OK);
        EXPECT_EQ(probe.requested, item[1]);
        xgl_window_destroy(&window);
        xgl_window_destroy(&window);
        EXPECT_EQ(probe.released, 1U);
    }
}

TEST(CompactWindowTest, FailedInitializationPreservesCallerState) {
    AllocationProbe probe;
    probe.fail = true;
    const xgm_allocator_t allocator = {&probe, allocate, release};
    xgl_sliding_window_t window = {};
    window.window_size = 9U;
    window.ack_head = 4U;
    window.send_base_packet_number = 123U;
    window.next_packet_number = 127U;
    const auto before = window;
    EXPECT_EQ(xgl_window_init_with_allocator(&window, 9U, &allocator),
              XGL_ERR_NO_MEMORY);
    EXPECT_EQ(window.window_size, before.window_size);
    EXPECT_EQ(window.ack_head, before.ack_head);
    EXPECT_EQ(window.send_base_packet_number, before.send_base_packet_number);
    EXPECT_EQ(window.next_packet_number, before.next_packet_number);
    EXPECT_EQ(window.ack_received, before.ack_received);
    EXPECT_EQ(window.allocator, before.allocator);
    EXPECT_EQ(probe.released, 0U);
}

TEST(CompactWindowTest, ReusesNonByteAlignedWindowWithoutStaleAcknowledgments) {
    AllocationProbe probe;
    const xgm_allocator_t allocator = {&probe, allocate, release};
    xgl_sliding_window_t window = {};
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 9U, &allocator), XGL_OK);
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
    EXPECT_EQ(probe.released, 1U);
}

TEST(CompactWindowTest, ResetClearsRotatedAcknowledgments) {
    AllocationProbe probe;
    const xgm_allocator_t allocator = {&probe, allocate, release};
    xgl_sliding_window_t window = {};
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 9U, &allocator), XGL_OK);
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
        AllocationProbe probe;
        const xgm_allocator_t allocator = {&probe, allocate, release};
        xgl_sliding_window_t window = {};
        ASSERT_EQ(xgl_window_init_with_allocator(&window, width, &allocator),
                  XGL_OK);
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
        EXPECT_EQ(probe.released, 1U);
    }
}
