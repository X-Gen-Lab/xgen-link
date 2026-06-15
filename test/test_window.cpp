/**
 * \file            test_window.cpp
 * \brief           Unit tests for production packet-number sliding window
 */

#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include "xgl/internal/xgl_allocator.h"
#include "xgl/internal/xgl_window.h"

template <typename T>
concept HasLegacySequenceWindowState = requires(T value) {
    value.send_base;
    value.next_seq_num;
    value.expected_seq_num;
};

static_assert(!HasLegacySequenceWindowState<xgl_sliding_window_t>,
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

static void* window_probe_malloc(size_t size) {
    if (g_window_alloc_probe != nullptr) {
        g_window_alloc_probe->alloc_count++;
    }
    void* ptr = std::malloc(size);
    if (g_window_alloc_probe != nullptr) {
        g_window_alloc_probe->last_ptr = ptr;
    }
    return ptr;
}

static void window_probe_free(void* ptr) {
    if (g_window_alloc_probe != nullptr) {
        g_window_alloc_probe->free_count++;
    }
    std::free(ptr);
}
}

TEST_F(XglWindowTest, InitSuccess) {
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);

    EXPECT_EQ(window.window_size, 8);
    EXPECT_EQ(window.send_base_packet_number, 0U);
    EXPECT_EQ(window.next_packet_number, 0U);
    EXPECT_NE(window.ack_received, nullptr);
}

TEST_F(XglWindowTest, InitUsesProvidedAllocator) {
    WindowAllocProbe probe = {};
    g_window_alloc_probe = &probe;
    xgl_allocator_t allocator = {
        .malloc = window_probe_malloc,
        .free = window_probe_free,
        .user_data = nullptr
    };

    ASSERT_EQ(xgl_window_init_with_allocator(&window, 8, &allocator), XGL_OK);
    EXPECT_EQ(probe.alloc_count, 1U);
    EXPECT_EQ(window.ack_received, probe.last_ptr);

    xgl_window_destroy(&window);
    EXPECT_EQ(probe.free_count, 1U);
    g_window_alloc_probe = nullptr;
}

TEST_F(XglWindowTest, InitRejectsInvalidParams) {
    EXPECT_EQ(xgl_window_init(nullptr, 8), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_window_init(&window, 0), XGL_ERR_INVALID_PARAM);
    EXPECT_EQ(xgl_window_init(&window, 129), XGL_ERR_INVALID_PARAM);
}

TEST_F(XglWindowTest, CanSendUntilWindowFull) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(xgl_window_can_send(&window));
        EXPECT_EQ(xgl_window_get_next_packet_number(&window), i);
        xgl_window_advance_next_packet_number(&window);
    }

    EXPECT_FALSE(xgl_window_can_send(&window));
    EXPECT_EQ(xgl_window_get_usage(&window), 4);
}

TEST_F(XglWindowTest, PacketNumbersDoNotWrapAtEightBits) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);

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
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);

    for (uint32_t i = 0; i < 5; ++i) {
        xgl_window_advance_next_packet_number(&window);
    }

    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 0U), XGL_OK);
    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 2U), XGL_OK);

    EXPECT_EQ(xgl_window_advance_base(&window), 1U);
    EXPECT_EQ(window.send_base_packet_number, 1U);
    EXPECT_EQ(xgl_window_get_usage(&window), 4U);

    ASSERT_EQ(xgl_window_mark_ack_packet_number(&window, 1U), XGL_OK);
    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 2U);
    EXPECT_EQ(window.send_base_packet_number, 3U);
    EXPECT_EQ(xgl_window_get_usage(&window), 2U);
}

TEST_F(XglWindowTest, MarkAckRejectsPacketsOutsideWindow) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);

    EXPECT_EQ(xgl_window_mark_ack_packet_number(&window, 4U),
              XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_window_mark_ack_packet_number(&window, UINT32_MAX),
              XGL_ERR_SEQUENCE_ERROR);
}

TEST_F(XglWindowTest, FullWindowCycle) {
    ASSERT_EQ(xgl_window_init(&window, 4), XGL_OK);

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
    EXPECT_TRUE(xgl_window_can_send(&window));
}

TEST_F(XglWindowTest, ResetClearsPacketNumberState) {
    ASSERT_EQ(xgl_window_init(&window, 8), XGL_OK);

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
    EXPECT_TRUE(xgl_window_can_send(&window));
}

TEST_F(XglWindowTest, NullPointerSafety) {
    EXPECT_FALSE(xgl_window_can_send(nullptr));
    EXPECT_EQ(xgl_window_get_next_packet_number(nullptr), 0U);
    xgl_window_advance_next_packet_number(nullptr);
    EXPECT_EQ(xgl_window_mark_ack_packet_number(nullptr, 0U), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_window_advance_base(nullptr), 0U);
    EXPECT_FALSE(xgl_window_is_in_window_packet_number(nullptr, 0U));
    EXPECT_EQ(xgl_window_get_usage(nullptr), 0U);
    xgl_window_reset(nullptr);
    xgl_window_destroy(nullptr);
}
