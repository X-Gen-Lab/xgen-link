/**
 * \file            test_serialization_properties.cpp
 * \brief           Serialization property tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include "property_framework.h"
#include <xgl/internal/xgl_serialize.h>

/**
 * \brief           Feature: x-gen-link, Property 3: Serialization Round-Trip
 * \details         For any multi-byte value, serializing it to little-endian
 *                  byte order and then deserializing should produce the
 *                  original value.
 *                  Validates: Requirements 12.1, 12.2, 51.2, 51.3
 */
TEST(XglSerializationProperties, SerializationRoundTrip) {
    PropertyTestGenerator gen;
    
    /* Test uint16_t serialization round-trip */
    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        uint16_t original_u16 = gen.random_uint16();
        uint8_t buffer_u16[2];
        
        /* Serialize */
        xgl_serialize_u16_le(buffer_u16, original_u16);
        
        /* Deserialize */
        uint16_t deserialized_u16 = xgl_deserialize_u16_le(buffer_u16);
        
        /* Verify round-trip */
        EXPECT_EQ(original_u16, deserialized_u16)
            << "uint16_t round-trip failed for value: " << original_u16;
    }
    
    /* Test uint32_t serialization round-trip */
    for (int i = 0; i < XGL_PROPERTY_TEST_ITERATIONS; ++i) {
        uint32_t original_u32 = gen.random_uint32();
        uint8_t buffer_u32[4];
        
        /* Serialize */
        xgl_serialize_u32_le(buffer_u32, original_u32);
        
        /* Deserialize */
        uint32_t deserialized_u32 = xgl_deserialize_u32_le(buffer_u32);
        
        /* Verify round-trip */
        EXPECT_EQ(original_u32, deserialized_u32)
            << "uint32_t round-trip failed for value: " << original_u32;
    }
}

/**
 * \brief           Test edge cases for serialization
 * \details         Tests boundary values (0, max, powers of 2)
 */
TEST(XglSerializationProperties, SerializationEdgeCases) {
    uint8_t buffer[4];
    
    /* Test uint16_t edge cases */
    uint16_t u16_values[] = {0, 1, 255, 256, 32767, 32768, 65535};
    for (size_t i = 0; i < sizeof(u16_values) / sizeof(u16_values[0]); ++i) {
        xgl_serialize_u16_le(buffer, u16_values[i]);
        uint16_t result = xgl_deserialize_u16_le(buffer);
        EXPECT_EQ(u16_values[i], result)
            << "uint16_t edge case failed for value: " << u16_values[i];
    }
    
    /* Test uint32_t edge cases */
    uint32_t u32_values[] = {0, 1, 255, 256, 65535, 65536, 
                             0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    for (size_t i = 0; i < sizeof(u32_values) / sizeof(u32_values[0]); ++i) {
        xgl_serialize_u32_le(buffer, u32_values[i]);
        uint32_t result = xgl_deserialize_u32_le(buffer);
        EXPECT_EQ(u32_values[i], result)
            << "uint32_t edge case failed for value: " << u32_values[i];
    }
}

/**
 * \brief           Test byte order correctness
 * \details         Verifies that serialization produces correct little-endian
 *                  byte order
 */
TEST(XglSerializationProperties, ByteOrderCorrectness) {
    uint8_t buffer[4];
    
    /* Test uint16_t byte order */
    xgl_serialize_u16_le(buffer, 0x1234);
    EXPECT_EQ(buffer[0], 0x34) << "uint16_t LSB incorrect";
    EXPECT_EQ(buffer[1], 0x12) << "uint16_t MSB incorrect";
    
    /* Test uint32_t byte order */
    xgl_serialize_u32_le(buffer, 0x12345678);
    EXPECT_EQ(buffer[0], 0x78) << "uint32_t byte 0 (LSB) incorrect";
    EXPECT_EQ(buffer[1], 0x56) << "uint32_t byte 1 incorrect";
    EXPECT_EQ(buffer[2], 0x34) << "uint32_t byte 2 incorrect";
    EXPECT_EQ(buffer[3], 0x12) << "uint32_t byte 3 (MSB) incorrect";
}

/**
 * \brief           Test NULL pointer handling
 * \details         Verifies that functions handle NULL pointers gracefully
 */
TEST(XglSerializationProperties, NullPointerHandling) {
    /* Test NULL buffer in serialize functions */
    xgl_serialize_u16_le(NULL, 0x1234);  /* Should not crash */
    xgl_serialize_u32_le(NULL, 0x12345678);  /* Should not crash */
    
    /* Test NULL buffer in deserialize functions */
    uint16_t result_u16 = xgl_deserialize_u16_le(NULL);
    EXPECT_EQ(result_u16, 0) << "deserialize_u16_le should return 0 for NULL";
    
    uint32_t result_u32 = xgl_deserialize_u32_le(NULL);
    EXPECT_EQ(result_u32, 0) << "deserialize_u32_le should return 0 for NULL";
}
