/**
 * \file            test_alignment_properties.cpp
 * \brief           Protocol frame roundtrips from unaligned byte spans
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_frame.h>

#include <array>
#include <cstring>
#include <gtest/gtest.h>

TEST(XglAlignmentProperties, CompleteFramesUseByteAlignedWireAccess) {
    const uint8_t payload[] = {0x01, 0xA5, 0x5A, 0xFF};
    xgl_frame_params_t params{};
    params.source_id = 0x1234;
    params.target_id = 0x5678;
    params.connection_id = 0xABCDEF01;
    params.packet_number = 0x12345678;
    params.payload = payload;
    params.payload_len = sizeof(payload);
    params.reliable = true;
    xgl_frame_t frame{};
    ASSERT_EQ(xgl_frame_build(&frame, &params), XGL_OK);
    std::array<uint8_t, 128> reference{};
    size_t expected_size = 0U;
    ASSERT_EQ(xgl_frame_serialize(reference.data(), reference.size(), &frame,
                                  &expected_size),
              XGL_OK);
    for (size_t offset = 1U; offset <= 7U; ++offset) {
        std::array<uint8_t, 136> storage{};
        storage.fill(0xCC);
        size_t written = 0U;
        ASSERT_EQ(xgl_frame_serialize(storage.data() + offset, 128U, &frame,
                                      &written),
                  XGL_OK);
        ASSERT_EQ(written, expected_size);
        EXPECT_EQ(std::memcmp(storage.data() + offset, reference.data(),
                              expected_size),
                  0);
        EXPECT_EQ(storage[offset - 1U], 0xCC);
        EXPECT_EQ(storage[offset + written], 0xCC);
        xgl_wire_frame_view_t view{};
        ASSERT_EQ(xgl_wire_decode_frame(&view, storage.data() + offset, written,
                                        nullptr),
                  XGL_OK);
        EXPECT_EQ(view.header.connection_id, params.connection_id);
        EXPECT_EQ(view.header.packet_number, params.packet_number);
        EXPECT_EQ(view.payload_len, sizeof(payload));
        EXPECT_EQ(std::memcmp(view.payload, payload, sizeof(payload)), 0);
    }
}
