/**
 * \file            test_wire_sack_view.cpp
 * \brief           Checked borrowed SACK values and legacy copy compatibility
 * \author          X-Gen Lab
 */

#include <wire/xgl_wire.h>

#include <array>
#include <gtest/gtest.h>

TEST(XglWireSackViewTest, BorrowsTheExactBitmapAndDecodesLittleEndianBase) {
    const uint8_t bytes[] = {0x78, 0x56, 0x34, 0x12, 3, 0x81, 0x42, 0x24};
    xgl_wire_sack_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_sack_view(bytes, sizeof(bytes), &view), XGL_OK);
    EXPECT_EQ(view.base_packet, UINT32_C(0x12345678));
    EXPECT_EQ(view.bitmap, bytes + 5U);
    EXPECT_EQ(view.bitmap_len, 3U);
    EXPECT_EQ(view.bitmap[0], 0x81U);
    EXPECT_EQ(view.bitmap[2], 0x24U);
}

TEST(XglWireSackViewTest, EmptyAndMaximumEncodedLengthsRetainWireSemantics) {
    const uint8_t empty[] = {7, 0, 0, 0, 0};
    xgl_wire_sack_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_sack_view(empty, sizeof(empty), &view), XGL_OK);
    EXPECT_EQ(view.base_packet, 7U);
    EXPECT_EQ(view.bitmap_len, 0U);
    EXPECT_EQ(view.bitmap, empty + sizeof(empty));

    std::array<uint8_t, 5U + UINT8_MAX> bytes = {};
    bytes[4] = UINT8_MAX;
    bytes.back() = 0x80;
    ASSERT_EQ(xgl_wire_decode_sack_view(bytes.data(), bytes.size(), &view),
              XGL_OK);
    EXPECT_EQ(view.bitmap, bytes.data() + 5U);
    EXPECT_EQ(view.bitmap_len, UINT8_MAX);
    EXPECT_EQ(view.bitmap[UINT8_MAX - 1U], 0x80U);
}

TEST(XglWireSackViewTest, EveryTruncationAndTrailingByteClearTheOutput) {
    const uint8_t bytes[] = {7, 0, 0, 0, 3, 1, 2, 4, 0xAA};
    xgl_wire_sack_view_t view = {};
    for (size_t size = 0U; size < 8U; ++size) {
        SCOPED_TRACE(size);
        ASSERT_EQ(xgl_wire_decode_sack_view(bytes, 8U, &view), XGL_OK);
        EXPECT_EQ(xgl_wire_decode_sack_view(bytes, size, &view),
                  XGL_ERR_INVALID_FRAME);
        EXPECT_EQ(view.base_packet, 0U);
        EXPECT_EQ(view.bitmap_len, 0U);
        EXPECT_EQ(view.bitmap, nullptr);
    }
    ASSERT_EQ(xgl_wire_decode_sack_view(bytes, 8U, &view), XGL_OK);
    EXPECT_EQ(xgl_wire_decode_sack_view(bytes, sizeof(bytes), &view),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(view.base_packet, 0U);
    EXPECT_EQ(view.bitmap_len, 0U);
    EXPECT_EQ(view.bitmap, nullptr);
}

TEST(XglWireSackViewTest, NullInputClearsAPreviouslySuccessfulView) {
    const uint8_t bytes[] = {7, 0, 0, 0, 1, 1};
    xgl_wire_sack_view_t view = {};
    ASSERT_EQ(xgl_wire_decode_sack_view(bytes, sizeof(bytes), &view), XGL_OK);
    EXPECT_EQ(xgl_wire_decode_sack_view(nullptr, 0U, &view),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(view.base_packet, 0U);
    EXPECT_EQ(view.bitmap_len, 0U);
    EXPECT_EQ(view.bitmap, nullptr);
    EXPECT_EQ(xgl_wire_decode_sack_view(bytes, sizeof(bytes), nullptr),
              XGL_ERR_NULL_POINTER);
}

TEST(XglWireSackViewTest, LegacyCopyPreservesOutputsUntilAllChecksSucceed) {
    const uint8_t bytes[] = {7, 0, 0, 0, 3, 1, 2, 4};
    uint8_t bitmap[] = {0xAA, 0xBB, 0xCC};
    uint32_t base = 99U;
    size_t length = 77U;
    EXPECT_EQ(xgl_wire_decode_sack_ext_value(bytes, sizeof(bytes), &base,
                                             bitmap, 2U, &length),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_wire_decode_sack_ext_value(bytes, sizeof(bytes), &base,
                                             nullptr, 0U, &length),
              XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_wire_decode_sack_ext_value(bytes, sizeof(bytes) - 1U, &base,
                                             bitmap, sizeof(bitmap), &length),
              XGL_ERR_INVALID_FRAME);
    EXPECT_EQ(base, 99U);
    EXPECT_EQ(length, 77U);
    EXPECT_EQ(bitmap[0], 0xAAU);
    EXPECT_EQ(bitmap[1], 0xBBU);
    EXPECT_EQ(bitmap[2], 0xCCU);

    ASSERT_EQ(xgl_wire_decode_sack_ext_value(bytes, sizeof(bytes), &base,
                                             bitmap, sizeof(bitmap), &length),
              XGL_OK);
    EXPECT_EQ(base, 7U);
    EXPECT_EQ(length, 3U);
    EXPECT_EQ(bitmap[0], 1U);
    EXPECT_EQ(bitmap[1], 2U);
    EXPECT_EQ(bitmap[2], 4U);
}
