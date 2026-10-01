#include <xgen/memory/libc_allocator.h>
/**
 * \file            test_window.cpp
 * \brief           Unit tests for production packet-number sliding window
 */

#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

#include "xgen/memory/allocator.h"
#include "transport/xgl_window.h"

template <typename T, typename = void>
struct HasLegacySequenceWindowState : std::false_type {};

template <typename T>
struct HasLegacySequenceWindowState<
    T, std::void_t<decltype(std::declval<T>().send_base),
                   decltype(std::declval<T>().next_seq_num),
                   decltype(std::declval<T>().expected_seq_num)>>
    : std::true_type {};

static_assert(
    !HasLegacySequenceWindowState<xgl_sliding_window_t>::value,
    "xgl_sliding_window_t must not expose legacy 8-bit sequence state");

class XglWindowTest : public ::testing::Test {
  protected:
    xgl_sliding_window_t window;

    void SetUp() override {
        std::memset(&window, 0, sizeof(window));
    }

    void TearDown() override {
        xgl_window_destroy(&window);
    }
};

namespace {
struct WindowAllocProbe {
    size_t alloc_count;
    size_t free_count;
    void* last_ptr;
};

static WindowAllocProbe* g_window_alloc_probe = nullptr;

static void* window_probe_malloc(void*, size_t size) {
    if (g_window_alloc_probe != nullptr) {
        g_window_alloc_probe->alloc_count++;
    }
    void* ptr = std::malloc(size);
    if (g_window_alloc_probe != nullptr) {
        g_window_alloc_probe->last_ptr = ptr;
    }
    return ptr;
}

static void window_probe_free(void*, void* ptr) {
    if (g_window_alloc_probe != nullptr) {
        g_window_alloc_probe->free_count++;
    }
    std::free(ptr);
}
}  // namespace

TEST_F(XglWindowTest, InitSuccess) {
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 8, xgm_allocator_libc()),
              XGL_OK);

    EXPECT_EQ(window.window_size, 8);
    EXPECT_EQ(window.send_base_packet_number, 0U);
    EXPECT_EQ(window.next_packet_number, 0U);
    EXPECT_NE(window.ack_received, nullptr);
}

TEST_F(XglWindowTest, InitUsesProvidedAllocator) {
    WindowAllocProbe probe = {};
    g_window_alloc_probe = &probe;
    xgm_allocator_t allocator = {};
    allocator.ctx = nullptr;
    allocator.alloc = window_probe_malloc;
    allocator.free = window_probe_free;

    ASSERT_EQ(xgl_window_init_with_allocator(&window, 8, &allocator), XGL_OK);
    EXPECT_EQ(probe.alloc_count, 1U);
    EXPECT_EQ(window.ack_received, probe.last_ptr);

    xgl_window_destroy(&window);
    EXPECT_EQ(probe.free_count, 1U);
    g_window_alloc_probe = nullptr;
}

TEST_F(XglWindowTest, InitRejectsInvalidParams) {
    EXPECT_EQ(xgl_window_init_with_allocator(nullptr, 8, xgm_allocator_libc()),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_window_init_with_allocator(&window, 0, xgm_allocator_libc()),
              XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(
        xgl_window_init_with_allocator(&window, 129, xgm_allocator_libc()),
        XGL_ERR_INVALID_PARAM);
}

TEST_F(XglWindowTest, CanSendUntilWindowFull) {
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 4, xgm_allocator_libc()),
              XGL_OK);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(xgl_window_can_send_packet_number(&window));
        EXPECT_EQ(xgl_window_get_next_packet_number(&window), i);
        xgl_window_advance_next_packet_number(&window);
    }

    EXPECT_FALSE(xgl_window_can_send_packet_number(&window));
    EXPECT_EQ(xgl_window_get_usage(&window), 4);
}

TEST_F(XglWindowTest, PacketNumbersDoNotWrapAtEightBits) {
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 4, xgm_allocator_libc()),
              XGL_OK);

    window.send_base_packet_number = 254U;
    window.next_packet_number = 254U;

    xgl_window_advance_next_packet_number(&window);
    xgl_window_advance_next_packet_number(&window);
    xgl_window_advance_next_packet_number(&window);

    EXPECT_EQ(xgl_window_get_next_packet_number(&window), 257U);
    EXPECT_TRUE(xgl_window_is_in_window_packet_number(&window, 254U));
    EXPECT_TRUE(xgl_window_is_in_window_packet_number(&window, 255U));
    EXPECT_TRUE(xgl_window_is_in_window_packet_number(&window, 256U));
    EXPECT_TRUE(xgl_window_is_in_window_packet_number(&window, 257U));
    EXPECT_FALSE(xgl_window_is_in_window_packet_number(&window, 258U));
}

TEST_F(XglWindowTest, AckRangeAdvancesBaseUntilGap) {
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 8, xgm_allocator_libc()),
              XGL_OK);

    for (uint32_t i = 0; i < 5; ++i) {
        xgl_window_advance_next_packet_number(&window);
    }

    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 0U), XGL_OK);
    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 2U), XGL_OK);

    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 1U);
    EXPECT_EQ(window.send_base_packet_number, 1U);
    EXPECT_EQ(xgl_window_get_usage(&window), 4U);

    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 1U), XGL_OK);
    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 2U);
    EXPECT_EQ(window.send_base_packet_number, 3U);
    EXPECT_EQ(xgl_window_get_usage(&window), 2U);
}

TEST_F(XglWindowTest, MarkAckRejectsPacketsOutsideWindow) {
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 4, xgm_allocator_libc()),
              XGL_OK);

    EXPECT_EQ(xgl_window_mark_ack_packet_number(&window, 4U),
              XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_window_mark_ack_packet_number(&window, UINT32_MAX),
              XGL_ERR_SEQUENCE_ERROR);
}

TEST_F(XglWindowTest, FullWindowCycle) {
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 4, xgm_allocator_libc()),
              XGL_OK);

    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(xgl_window_can_send_packet_number(&window));
        xgl_window_advance_next_packet_number(&window);
    }
    ASSERT_FALSE(xgl_window_can_send_packet_number(&window));

    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, i), XGL_OK);
    }

    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 4U);
    EXPECT_EQ(xgl_window_get_usage(&window), 0U);
    EXPECT_TRUE(xgl_window_can_send_packet_number(&window));
}

TEST_F(XglWindowTest, ResetClearsPacketNumberState) {
    ASSERT_EQ(xgl_window_init_with_allocator(&window, 8, xgm_allocator_libc()),
              XGL_OK);

    for (uint32_t i = 0; i < 5; ++i) {
        xgl_window_advance_next_packet_number(&window);
    }
    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 0U), XGL_OK);
    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 1U), XGL_OK);
    ASSERT_EQ(xgl_window_advance_base_packet_number(&window), 2U);

    xgl_window_reset(&window);

    EXPECT_EQ(window.send_base_packet_number, 0U);
    EXPECT_EQ(window.next_packet_number, 0U);
    EXPECT_EQ(xgl_window_get_usage(&window), 0U);
    EXPECT_TRUE(xgl_window_can_send_packet_number(&window));
}

TEST_F(XglWindowTest, NullPointerSafety) {
    EXPECT_FALSE(xgl_window_can_send_packet_number(nullptr));
    EXPECT_EQ(xgl_window_get_next_packet_number(nullptr), 0U);
    xgl_window_advance_next_packet_number(nullptr);
    EXPECT_EQ(xgl_window_mark_ack_packet_number(nullptr, 0U),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_window_advance_base_packet_number(nullptr), 0U);
    EXPECT_FALSE(xgl_window_is_in_window_packet_number(nullptr, 0U));
    EXPECT_EQ(xgl_window_get_usage(nullptr), 0U);
    xgl_window_reset(nullptr);
    xgl_window_destroy(nullptr);
}
