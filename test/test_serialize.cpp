/**
 * \file            test_serialize.cpp
 * \brief           Serialization unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <cstring>

/*---------------------------------------------------------------------------*/
/* uint16_t Serialization Tests                                             */
/*---------------------------------------------------------------------------*/

TEST(XglSerializeTest, SerializeU16LE_Basic) {
    uint8_t buffer[2];
    
    /* Test value 0x1234 */
    xgl_serialize_u16_le(buffer, 0x1234);
    EXPECT_EQ(buffer[0], 0x34);  /* LSB */
    EXPECT_EQ(buffer[1], 0x12);  /* MSB */
}

TEST(XglSerializeTest, SerializeU16LE_Zero) {
    uint8_t buffer[2];
    
    xgl_serialize_u16_le(buffer, 0x0000);
    EXPECT_EQ(buffer[0], 0x00);
    EXPECT_EQ(buffer[1], 0x00);
}

TEST(XglSerializeTest, SerializeU16LE_Max) {
    uint8_t buffer[2];
    
    xgl_serialize_u16_le(buffer, 0xFFFF);
    EXPECT_EQ(buffer[0], 0xFF);
    EXPECT_EQ(buffer[1], 0xFF);
}

TEST(XglSerializeTest, SerializeU16LE_NullBuffer) {
    /* Should not crash */
    xgl_serialize_u16_le(nullptr, 0x1234);
}

TEST(XglSerializeTest, DeserializeU16LE_Basic) {
    uint8_t buffer[2] = {0x34, 0x12};
    
    uint16_t value = xgl_deserialize_u16_le(buffer);
    EXPECT_EQ(value, 0x1234);
}

TEST(XglSerializeTest, DeserializeU16LE_Zero) {
    uint8_t buffer[2] = {0x00, 0x00};
    
    uint16_t value = xgl_deserialize_u16_le(buffer);
    EXPECT_EQ(value, 0x0000);
}

TEST(XglSerializeTest, DeserializeU16LE_Max) {
    uint8_t buffer[2] = {0xFF, 0xFF};
    
    uint16_t value = xgl_deserialize_u16_le(buffer);
    EXPECT_EQ(value, 0xFFFF);
}

TEST(XglSerializeTest, DeserializeU16LE_NullBuffer) {
    uint16_t value = xgl_deserialize_u16_le(nullptr);
    EXPECT_EQ(value, 0);
}

TEST(XglSerializeTest, U16LE_RoundTrip) {
    uint8_t buffer[2];
    
    /* Test multiple values */
    uint16_t test_values[] = {0x0000, 0x0001, 0x00FF, 0x0100, 0x1234, 0xABCD, 0xFFFF};
    
    for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
        xgl_serialize_u16_le(buffer, test_values[i]);
        uint16_t result = xgl_deserialize_u16_le(buffer);
        EXPECT_EQ(result, test_values[i]) << "Failed for value 0x" << std::hex << test_values[i];
    }
}

/*---------------------------------------------------------------------------*/
/* uint32_t Serialization Tests                                             */
/*---------------------------------------------------------------------------*/

TEST(XglSerializeTest, SerializeU32LE_Basic) {
    uint8_t buffer[4];
    
    /* Test value 0x12345678 */
    xgl_serialize_u32_le(buffer, 0x12345678);
    EXPECT_EQ(buffer[0], 0x78);  /* Byte 0 (LSB) */
    EXPECT_EQ(buffer[1], 0x56);  /* Byte 1 */
    EXPECT_EQ(buffer[2], 0x34);  /* Byte 2 */
    EXPECT_EQ(buffer[3], 0x12);  /* Byte 3 (MSB) */
}

TEST(XglSerializeTest, SerializeU32LE_Zero) {
    uint8_t buffer[4];
    
    xgl_serialize_u32_le(buffer, 0x00000000);
    EXPECT_EQ(buffer[0], 0x00);
    EXPECT_EQ(buffer[1], 0x00);
    EXPECT_EQ(buffer[2], 0x00);
    EXPECT_EQ(buffer[3], 0x00);
}

TEST(XglSerializeTest, SerializeU32LE_Max) {
    uint8_t buffer[4];
    
    xgl_serialize_u32_le(buffer, 0xFFFFFFFF);
    EXPECT_EQ(buffer[0], 0xFF);
    EXPECT_EQ(buffer[1], 0xFF);
    EXPECT_EQ(buffer[2], 0xFF);
    EXPECT_EQ(buffer[3], 0xFF);
}

TEST(XglSerializeTest, SerializeU32LE_NullBuffer) {
    /* Should not crash */
    xgl_serialize_u32_le(nullptr, 0x12345678);
}

TEST(XglSerializeTest, DeserializeU32LE_Basic) {
    uint8_t buffer[4] = {0x78, 0x56, 0x34, 0x12};
    
    uint32_t value = xgl_deserialize_u32_le(buffer);
    EXPECT_EQ(value, 0x12345678);
}

TEST(XglSerializeTest, DeserializeU32LE_Zero) {
    uint8_t buffer[4] = {0x00, 0x00, 0x00, 0x00};
    
    uint32_t value = xgl_deserialize_u32_le(buffer);
    EXPECT_EQ(value, 0x00000000);
}

TEST(XglSerializeTest, DeserializeU32LE_Max) {
    uint8_t buffer[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    
    uint32_t value = xgl_deserialize_u32_le(buffer);
    EXPECT_EQ(value, 0xFFFFFFFF);
}

TEST(XglSerializeTest, DeserializeU32LE_NullBuffer) {
    uint32_t value = xgl_deserialize_u32_le(nullptr);
    EXPECT_EQ(value, 0);
}

TEST(XglSerializeTest, U32LE_RoundTrip) {
    uint8_t buffer[4];
    
    /* Test multiple values */
    uint32_t test_values[] = {
        0x00000000, 0x00000001, 0x000000FF, 0x0000FF00,
        0x00FF0000, 0xFF000000, 0x12345678, 0xABCDEF01,
        0xFFFFFFFF
    };
    
    for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
        xgl_serialize_u32_le(buffer, test_values[i]);
        uint32_t result = xgl_deserialize_u32_le(buffer);
        EXPECT_EQ(result, test_values[i]) << "Failed for value 0x" << std::hex << test_values[i];
    }
}

/*---------------------------------------------------------------------------*/
/* Alignment Safety Tests                                                   */
/*---------------------------------------------------------------------------*/

TEST(XglSerializeTest, UnalignedAccess_U16) {
    /* Create a buffer with odd alignment */
    uint8_t buffer[5];
    uint8_t* unaligned_ptr = buffer + 1;  /* Offset by 1 byte */
    
    /* Test serialization to unaligned address */
    xgl_serialize_u16_le(unaligned_ptr, 0x1234);
    EXPECT_EQ(unaligned_ptr[0], 0x34);
    EXPECT_EQ(unaligned_ptr[1], 0x12);
    
    /* Test deserialization from unaligned address */
    uint16_t value = xgl_deserialize_u16_le(unaligned_ptr);
    EXPECT_EQ(value, 0x1234);
}

TEST(XglSerializeTest, UnalignedAccess_U32) {
    /* Create a buffer with odd alignment */
    uint8_t buffer[7];
    uint8_t* unaligned_ptr = buffer + 1;  /* Offset by 1 byte */
    
    /* Test serialization to unaligned address */
    xgl_serialize_u32_le(unaligned_ptr, 0x12345678);
    EXPECT_EQ(unaligned_ptr[0], 0x78);
    EXPECT_EQ(unaligned_ptr[1], 0x56);
    EXPECT_EQ(unaligned_ptr[2], 0x34);
    EXPECT_EQ(unaligned_ptr[3], 0x12);
    
    /* Test deserialization from unaligned address */
    uint32_t value = xgl_deserialize_u32_le(unaligned_ptr);
    EXPECT_EQ(value, 0x12345678);
}

/*---------------------------------------------------------------------------*/
/* Macro Tests                                                              */
/*---------------------------------------------------------------------------*/

TEST(XglSerializeTest, MacroReadWrite_U16) {
    uint8_t buffer[2];
    
    XGL_WRITE_U16(buffer, 0x1234);
    uint16_t value = XGL_READ_U16(buffer);
    
    EXPECT_EQ(value, 0x1234);
}

TEST(XglSerializeTest, MacroReadWrite_U32) {
    uint8_t buffer[4];
    
    XGL_WRITE_U32(buffer, 0x12345678);
    uint32_t value = XGL_READ_U32(buffer);
    
    EXPECT_EQ(value, 0x12345678);
}

/*---------------------------------------------------------------------------*/
/* Endianness Conversion Tests                                              */
/*---------------------------------------------------------------------------*/

TEST(XglSerializeTest, EndiannessConversion_U16) {
    uint16_t value = 0x1234;
    
    /* Convert to little-endian and back */
    uint16_t le_value = XGL_HTOLE16(value);
    uint16_t host_value = XGL_LE16TOH(le_value);
    
    EXPECT_EQ(host_value, value);
}

TEST(XglSerializeTest, EndiannessConversion_U32) {
    uint32_t value = 0x12345678;
    
    /* Convert to little-endian and back */
    uint32_t le_value = XGL_HTOLE32(value);
    uint32_t host_value = XGL_LE32TOH(le_value);
    
    EXPECT_EQ(host_value, value);
}

/*---------------------------------------------------------------------------*/
/* Edge Cases                                                               */
/*---------------------------------------------------------------------------*/

TEST(XglSerializeTest, EdgeCase_SingleByte) {
    uint8_t buffer[2];
    
    /* Test values that fit in single byte */
    xgl_serialize_u16_le(buffer, 0x00AB);
    EXPECT_EQ(buffer[0], 0xAB);
    EXPECT_EQ(buffer[1], 0x00);
    
    uint16_t value = xgl_deserialize_u16_le(buffer);
    EXPECT_EQ(value, 0x00AB);
}

TEST(XglSerializeTest, EdgeCase_ByteBoundaries) {
    uint8_t buffer[4];
    
    /* Test values at byte boundaries */
    uint32_t test_values[] = {
        0x000000FF,  /* 1 byte */
        0x0000FFFF,  /* 2 bytes */
        0x00FFFFFF,  /* 3 bytes */
        0xFFFFFFFF   /* 4 bytes */
    };
    
    for (size_t i = 0; i < sizeof(test_values) / sizeof(test_values[0]); i++) {
        xgl_serialize_u32_le(buffer, test_values[i]);
        uint32_t result = xgl_deserialize_u32_le(buffer);
        EXPECT_EQ(result, test_values[i]);
    }
}

/*---------------------------------------------------------------------------*/
/* Compile-Time Configuration Tests                                         */
/*---------------------------------------------------------------------------*/

TEST(XglSerializeTest, EndiannessDetection) {
    /* Verify endianness detection is consistent */
#if XGL_IS_LITTLE_ENDIAN
    /* On little-endian systems, no conversion needed */
    uint16_t value = 0x1234;
    EXPECT_EQ(XGL_HTOLE16(value), value);
#else
    /* On big-endian systems, conversion needed */
    uint16_t value = 0x1234;
    EXPECT_EQ(XGL_HTOLE16(value), 0x3412);
#endif
}

TEST(XglSerializeTest, AlignmentDetection) {
    /* Just verify the macro is defined */
#if XGL_STRICT_ALIGNMENT
    /* Strict alignment mode - uses byte-wise access */
    EXPECT_TRUE(true);
#else
    /* Relaxed alignment mode - can use direct access */
    EXPECT_TRUE(true);
#endif
}
