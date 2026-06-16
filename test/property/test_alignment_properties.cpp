/**
 * \file            test_alignment_properties.cpp
 * \brief           Alignment safety property tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include "property_framework.h"
#include <xgl/internal/xgl_serialize.h>
#include <cstring>
#include <vector>

/**
 * \brief           Feature: x-gen-link, Property 28: Alignment Safety
 * \details         For any multi-byte field access, the protocol should use
 *                  byte-wise access on strict-alignment platforms (ARM Cortex-M0).
 *                  Validates: Requirements 12.5, 52.2
 */
TEST(XglAlignmentProperties, AlignmentSafety) {
    PropertyTestGenerator gen;

    /* Test uint16_t alignment safety */
    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        uint16_t original_u16 = gen.random_uint16();

        /* Create buffer with extra space for unaligned access */
        std::vector<uint8_t> buffer(10, 0);

        /* Test all possible alignments (0, 1, 2, 3 byte offsets) */
        for (size_t offset = 0; offset < 4; ++offset) {
            uint8_t* unaligned_ptr = buffer.data() + offset;

            /* Write using alignment-safe macro */
            XGL_WRITE_U16(unaligned_ptr, original_u16);

            /* Read using alignment-safe macro */
            uint16_t read_value = XGL_READ_U16(unaligned_ptr);

            /* Verify correctness */
            EXPECT_EQ(original_u16, read_value)
                << "uint16_t alignment safety failed at offset " << offset
                << " for value: " << original_u16;

            /* Verify byte-wise correctness */
            EXPECT_EQ(unaligned_ptr[0], (uint8_t)(original_u16 & 0xFF))
                << "uint16_t LSB incorrect at offset " << offset;
            EXPECT_EQ(unaligned_ptr[1], (uint8_t)((original_u16 >> 8) & 0xFF))
                << "uint16_t MSB incorrect at offset " << offset;
        }
    }

    /* Test uint32_t alignment safety */
    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        uint32_t original_u32 = gen.random_uint32();

        /* Create buffer with extra space for unaligned access */
        std::vector<uint8_t> buffer(12, 0);

        /* Test all possible alignments (0, 1, 2, 3 byte offsets) */
        for (size_t offset = 0; offset < 4; ++offset) {
            uint8_t* unaligned_ptr = buffer.data() + offset;

            /* Write using alignment-safe macro */
            XGL_WRITE_U32(unaligned_ptr, original_u32);

            /* Read using alignment-safe macro */
            uint32_t read_value = XGL_READ_U32(unaligned_ptr);

            /* Verify correctness */
            EXPECT_EQ(original_u32, read_value)
                << "uint32_t alignment safety failed at offset " << offset
                << " for value: " << original_u32;

            /* Verify byte-wise correctness */
            EXPECT_EQ(unaligned_ptr[0], (uint8_t)(original_u32 & 0xFF))
                << "uint32_t byte 0 incorrect at offset " << offset;
            EXPECT_EQ(unaligned_ptr[1], (uint8_t)((original_u32 >> 8) & 0xFF))
                << "uint32_t byte 1 incorrect at offset " << offset;
            EXPECT_EQ(unaligned_ptr[2], (uint8_t)((original_u32 >> 16) & 0xFF))
                << "uint32_t byte 2 incorrect at offset " << offset;
            EXPECT_EQ(unaligned_ptr[3], (uint8_t)((original_u32 >> 24) & 0xFF))
                << "uint32_t byte 3 incorrect at offset " << offset;
        }
    }
}

/**
 * \brief           Test serialization functions with unaligned buffers
 * \details         Verifies that serialization functions work correctly
 *                  with unaligned buffer pointers
 */
TEST(XglAlignmentProperties, SerializationUnalignedBuffers) {
    PropertyTestGenerator gen;

    /* Test uint16_t serialization with unaligned buffers */
    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        uint16_t original_u16 = gen.random_uint16();

        /* Create buffer with extra space */
        std::vector<uint8_t> buffer(10, 0);

        /* Test all possible alignments */
        for (size_t offset = 0; offset < 4; ++offset) {
            uint8_t* unaligned_ptr = buffer.data() + offset;

            /* Serialize to unaligned buffer */
            xgl_serialize_u16_le(unaligned_ptr, original_u16);

            /* Deserialize from unaligned buffer */
            uint16_t deserialized = xgl_deserialize_u16_le(unaligned_ptr);

            /* Verify round-trip */
            EXPECT_EQ(original_u16, deserialized)
                << "uint16_t serialization failed at offset " << offset
                << " for value: " << original_u16;
        }
    }

    /* Test uint32_t serialization with unaligned buffers */
    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        uint32_t original_u32 = gen.random_uint32();

        /* Create buffer with extra space */
        std::vector<uint8_t> buffer(12, 0);

        /* Test all possible alignments */
        for (size_t offset = 0; offset < 4; ++offset) {
            uint8_t* unaligned_ptr = buffer.data() + offset;

            /* Serialize to unaligned buffer */
            xgl_serialize_u32_le(unaligned_ptr, original_u32);

            /* Deserialize from unaligned buffer */
            uint32_t deserialized = xgl_deserialize_u32_le(unaligned_ptr);

            /* Verify round-trip */
            EXPECT_EQ(original_u32, deserialized)
                << "uint32_t serialization failed at offset " << offset
                << " for value: " << original_u32;
        }
    }
}

/**
 * \brief           Test edge cases for alignment safety
 * \details         Tests boundary values with unaligned access
 */
TEST(XglAlignmentProperties, AlignmentEdgeCases) {
    /* Test uint16_t edge cases with unaligned access */
    uint16_t u16_values[] = {0, 1, 255, 256, 32767, 32768, 65535};
    std::vector<uint8_t> buffer(10, 0);

    for (size_t i = 0; i < sizeof(u16_values) / sizeof(u16_values[0]); ++i) {
        for (size_t offset = 0; offset < 4; ++offset) {
            uint8_t* unaligned_ptr = buffer.data() + offset;

            XGL_WRITE_U16(unaligned_ptr, u16_values[i]);
            uint16_t result = XGL_READ_U16(unaligned_ptr);

            EXPECT_EQ(u16_values[i], result)
                << "uint16_t edge case failed at offset " << offset
                << " for value: " << u16_values[i];
        }
    }

    /* Test uint32_t edge cases with unaligned access */
    uint32_t u32_values[] = {0, 1, 255, 256, 65535, 65536,
                             0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};

    for (size_t i = 0; i < sizeof(u32_values) / sizeof(u32_values[0]); ++i) {
        for (size_t offset = 0; offset < 4; ++offset) {
            uint8_t* unaligned_ptr = buffer.data() + offset;

            XGL_WRITE_U32(unaligned_ptr, u32_values[i]);
            uint32_t result = XGL_READ_U32(unaligned_ptr);

            EXPECT_EQ(u32_values[i], result)
                << "uint32_t edge case failed at offset " << offset
                << " for value: " << u32_values[i];
        }
    }
}

/**
 * \brief           Test alignment detection macros
 * \details         Verifies that alignment detection macros are defined correctly
 */
TEST(XglAlignmentProperties, AlignmentDetection) {
    /* Verify that XGL_STRICT_ALIGNMENT is defined */
#ifdef XGL_STRICT_ALIGNMENT
    /* Check that it's either 0 or 1 */
    EXPECT_TRUE(XGL_STRICT_ALIGNMENT == 0 || XGL_STRICT_ALIGNMENT == 1)
        << "XGL_STRICT_ALIGNMENT should be 0 or 1";
#else
    FAIL() << "XGL_STRICT_ALIGNMENT should be defined";
#endif

    /* Log the alignment mode for debugging */
#if XGL_STRICT_ALIGNMENT
    std::cout << "Running in STRICT ALIGNMENT mode (byte-wise access)" << std::endl;
#else
    std::cout << "Running in RELAXED ALIGNMENT mode (direct access)" << std::endl;
#endif
}

/**
 * \brief           Test that byte-wise access produces correct results
 * \details         Verifies that byte-wise serialization/deserialization
 *                  produces the same results as direct memory access
 */
TEST(XglAlignmentProperties, ByteWiseAccessCorrectness) {
    PropertyTestGenerator gen;

    /* Test uint16_t byte-wise access */
    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        uint16_t original = gen.random_uint16();

        /* Method 1: Byte-wise serialization */
        uint8_t buffer1[2];
        xgl_serialize_u16_le(buffer1, original);

        /* Method 2: Manual byte extraction */
        uint8_t buffer2[2];
        buffer2[0] = (uint8_t)(original & 0xFF);
        buffer2[1] = (uint8_t)((original >> 8) & 0xFF);

        /* Verify both methods produce same result */
        EXPECT_EQ(buffer1[0], buffer2[0])
            << "uint16_t byte 0 mismatch for value: " << original;
        EXPECT_EQ(buffer1[1], buffer2[1])
            << "uint16_t byte 1 mismatch for value: " << original;

        /* Verify deserialization */
        uint16_t result1 = xgl_deserialize_u16_le(buffer1);
        uint16_t result2 = ((uint16_t)buffer2[1] << 8) | buffer2[0];

        EXPECT_EQ(result1, result2)
            << "uint16_t deserialization mismatch";
        EXPECT_EQ(result1, original)
            << "uint16_t round-trip failed";
    }

    /* Test uint32_t byte-wise access */
    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        uint32_t original = gen.random_uint32();

        /* Method 1: Byte-wise serialization */
        uint8_t buffer1[4];
        xgl_serialize_u32_le(buffer1, original);

        /* Method 2: Manual byte extraction */
        uint8_t buffer2[4];
        buffer2[0] = (uint8_t)(original & 0xFF);
        buffer2[1] = (uint8_t)((original >> 8) & 0xFF);
        buffer2[2] = (uint8_t)((original >> 16) & 0xFF);
        buffer2[3] = (uint8_t)((original >> 24) & 0xFF);

        /* Verify both methods produce same result */
        for (int j = 0; j < 4; ++j) {
            EXPECT_EQ(buffer1[j], buffer2[j])
                << "uint32_t byte " << j << " mismatch for value: " << original;
        }

        /* Verify deserialization */
        uint32_t result1 = xgl_deserialize_u32_le(buffer1);
        uint32_t result2 = ((uint32_t)buffer2[3] << 24) |
                          ((uint32_t)buffer2[2] << 16) |
                          ((uint32_t)buffer2[1] << 8) |
                          buffer2[0];

        EXPECT_EQ(result1, result2)
            << "uint32_t deserialization mismatch";
        EXPECT_EQ(result1, original)
            << "uint32_t round-trip failed";
    }
}

/**
 * \brief           Test alignment safety with real-world frame data
 * \details         Simulates reading multi-byte fields from frame buffers
 *                  at various alignments
 */
TEST(XglAlignmentProperties, FrameFieldAlignment) {
    PropertyTestGenerator gen;

    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        /* Generate random frame-like data */
        std::vector<uint8_t> frame_buffer(32, 0);

        /* Populate with random data */
        for (size_t j = 0; j < frame_buffer.size(); ++j) {
            frame_buffer[j] = gen.random_uint8();
        }

        /* Test reading uint16_t fields at various positions */
        for (size_t offset = 0; offset < frame_buffer.size() - 2; ++offset) {
            uint8_t* field_ptr = frame_buffer.data() + offset;

            /* Write a known value */
            uint16_t test_value = gen.random_uint16();
            XGL_WRITE_U16(field_ptr, test_value);

            /* Read it back */
            uint16_t read_value = XGL_READ_U16(field_ptr);

            EXPECT_EQ(test_value, read_value)
                << "Frame field read failed at offset " << offset;
        }

        /* Test reading uint32_t fields at various positions */
        for (size_t offset = 0; offset < frame_buffer.size() - 4; ++offset) {
            uint8_t* field_ptr = frame_buffer.data() + offset;

            /* Write a known value */
            uint32_t test_value = gen.random_uint32();
            XGL_WRITE_U32(field_ptr, test_value);

            /* Read it back */
            uint32_t read_value = XGL_READ_U32(field_ptr);

            EXPECT_EQ(test_value, read_value)
                << "Frame field read failed at offset " << offset;
        }
    }
}
