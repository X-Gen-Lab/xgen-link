/**
 * \file            test_platform.cpp
 * \brief           Platform detection tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_platform.h>
#include <cstring>

/*---------------------------------------------------------------------------*/
/* Platform Detection Tests                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test platform info retrieval
 */
TEST(XglPlatformTest, GetPlatformInfo) {
    xgl_platform_info_t info;
    memset(&info, 0, sizeof(info));
    
    xgl_platform_get_info(&info);
    
    /* Verify all fields are populated */
    EXPECT_NE(info.compiler_name, nullptr);
    EXPECT_NE(info.os_name, nullptr);
    EXPECT_NE(info.arch_name, nullptr);
    EXPECT_NE(info.endian_name, nullptr);
    EXPECT_NE(info.alignment_name, nullptr);
    
    /* Verify pointer size is reasonable */
    EXPECT_TRUE(info.pointer_size == 4 || info.pointer_size == 8);
    
    /* Verify alignment is power of 2 */
    EXPECT_TRUE(info.alignment_required == 1 || 
                info.alignment_required == 2 || 
                info.alignment_required == 4 || 
                info.alignment_required == 8);
    
    /* Verify endianness flag matches runtime check */
    EXPECT_EQ(info.is_little_endian, xgl_is_little_endian());
    
    /* Verify 64-bit flag matches pointer size */
    if (info.pointer_size == 8) {
        EXPECT_EQ(info.is_64bit, 1);
    } else {
        EXPECT_EQ(info.is_64bit, 0);
    }
}

/**
 * \brief           Test platform info string generation
 */
TEST(XglPlatformTest, PlatformInfoString) {
    char buffer[512];
    
    int written = xgl_platform_info_string(buffer, sizeof(buffer));
    
    /* Verify something was written */
    EXPECT_GT(written, 0);
    EXPECT_LT(written, (int)sizeof(buffer));
    
    /* Verify null termination */
    EXPECT_EQ(buffer[written], '\0');
    
    /* Verify expected content */
    EXPECT_NE(strstr(buffer, "Compiler:"), nullptr);
    EXPECT_NE(strstr(buffer, "OS:"), nullptr);
    EXPECT_NE(strstr(buffer, "Architecture:"), nullptr);
    EXPECT_NE(strstr(buffer, "Pointer Size:"), nullptr);
    EXPECT_NE(strstr(buffer, "Endianness:"), nullptr);
    EXPECT_NE(strstr(buffer, "Alignment:"), nullptr);
}

/**
 * \brief           Test platform info string with small buffer
 */
TEST(XglPlatformTest, PlatformInfoStringSmallBuffer) {
    char buffer[32];
    memset(buffer, 0, sizeof(buffer));
    
    int written = xgl_platform_info_string(buffer, sizeof(buffer));
    
    /* Should write something but not overflow */
    EXPECT_GT(written, 0);
    EXPECT_LT(written, (int)sizeof(buffer));
    
    /* Buffer should be null-terminated within bounds */
    EXPECT_EQ(buffer[sizeof(buffer) - 1], '\0');
}

/**
 * \brief           Test platform info string with NULL buffer
 */
TEST(XglPlatformTest, PlatformInfoStringNullBuffer) {
    int written = xgl_platform_info_string(NULL, 100);
    
    /* Should return 0 for NULL buffer */
    EXPECT_EQ(written, 0);
}

/**
 * \brief           Test platform info string with zero size
 */
TEST(XglPlatformTest, PlatformInfoStringZeroSize) {
    char buffer[32];
    
    int written = xgl_platform_info_string(buffer, 0);
    
    /* Should return 0 for zero size */
    EXPECT_EQ(written, 0);
}

/*---------------------------------------------------------------------------*/
/* Endianness Tests                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test endianness detection
 */
TEST(XglPlatformTest, EndiannessDetection) {
    int is_little = xgl_is_little_endian();
    
    /* Verify it returns a boolean value */
    EXPECT_TRUE(is_little == 0 || is_little == 1);
    
    /* Verify consistency with compile-time detection */
#ifdef XGL_LITTLE_ENDIAN
    EXPECT_EQ(is_little, 1);
#endif
    
#ifdef XGL_BIG_ENDIAN
    EXPECT_EQ(is_little, 0);
#endif
}

/**
 * \brief           Test endianness with known value
 */
TEST(XglPlatformTest, EndiannessKnownValue) {
    uint32_t test = 0x01020304;
    uint8_t* bytes = (uint8_t*)&test;
    
    if (xgl_is_little_endian()) {
        /* Little-endian: LSB first */
        EXPECT_EQ(bytes[0], 0x04);
        EXPECT_EQ(bytes[1], 0x03);
        EXPECT_EQ(bytes[2], 0x02);
        EXPECT_EQ(bytes[3], 0x01);
    } else {
        /* Big-endian: MSB first */
        EXPECT_EQ(bytes[0], 0x01);
        EXPECT_EQ(bytes[1], 0x02);
        EXPECT_EQ(bytes[2], 0x03);
        EXPECT_EQ(bytes[3], 0x04);
    }
}

/*---------------------------------------------------------------------------*/
/* Alignment Tests                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test alignment checking
 */
TEST(XglPlatformTest, AlignmentCheck) {
    uint8_t buffer[16];
    
    /* Test various alignments */
    for (size_t i = 0; i < 16; i++) {
        void* ptr = &buffer[i];
        
        /* 1-byte alignment always succeeds */
        EXPECT_TRUE(xgl_is_aligned(ptr, 1));
        
        /* 2-byte alignment */
        if (i % 2 == 0) {
            EXPECT_TRUE(xgl_is_aligned(ptr, 2));
        } else {
            EXPECT_FALSE(xgl_is_aligned(ptr, 2));
        }
        
        /* 4-byte alignment */
        if (i % 4 == 0) {
            EXPECT_TRUE(xgl_is_aligned(ptr, 4));
        } else {
            EXPECT_FALSE(xgl_is_aligned(ptr, 4));
        }
        
        /* 8-byte alignment */
        if (i % 8 == 0) {
            EXPECT_TRUE(xgl_is_aligned(ptr, 8));
        } else {
            EXPECT_FALSE(xgl_is_aligned(ptr, 8));
        }
    }
}

/**
 * \brief           Test pointer alignment
 */
TEST(XglPlatformTest, AlignPointer) {
    uint8_t buffer[32];
    
    for (size_t i = 0; i < 16; i++) {
        void* ptr = &buffer[i];
        
        /* Align to 4 bytes */
        void* aligned = xgl_align_up(ptr, 4);
        EXPECT_TRUE(xgl_is_aligned(aligned, 4));
        EXPECT_GE(aligned, ptr);
        EXPECT_LT((uintptr_t)aligned - (uintptr_t)ptr, 4);
        
        /* Align to 8 bytes */
        aligned = xgl_align_up(ptr, 8);
        EXPECT_TRUE(xgl_is_aligned(aligned, 8));
        EXPECT_GE(aligned, ptr);
        EXPECT_LT((uintptr_t)aligned - (uintptr_t)ptr, 8);
    }
}

/**
 * \brief           Test size alignment
 */
TEST(XglPlatformTest, AlignSize) {
    /* Test various sizes */
    EXPECT_EQ(xgl_align_size(0, 4), 0);
    EXPECT_EQ(xgl_align_size(1, 4), 4);
    EXPECT_EQ(xgl_align_size(2, 4), 4);
    EXPECT_EQ(xgl_align_size(3, 4), 4);
    EXPECT_EQ(xgl_align_size(4, 4), 4);
    EXPECT_EQ(xgl_align_size(5, 4), 8);
    EXPECT_EQ(xgl_align_size(8, 4), 8);
    EXPECT_EQ(xgl_align_size(9, 4), 12);
    
    /* Test 8-byte alignment */
    EXPECT_EQ(xgl_align_size(0, 8), 0);
    EXPECT_EQ(xgl_align_size(1, 8), 8);
    EXPECT_EQ(xgl_align_size(7, 8), 8);
    EXPECT_EQ(xgl_align_size(8, 8), 8);
    EXPECT_EQ(xgl_align_size(9, 8), 16);
}

/*---------------------------------------------------------------------------*/
/* Compiler Detection Tests                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test compiler detection
 */
TEST(XglPlatformTest, CompilerDetection) {
    xgl_platform_info_t info;
    xgl_platform_get_info(&info);
    
    /* Verify compiler name is not "Unknown" */
    EXPECT_STRNE(info.compiler_name, "Unknown");
    
    /* Verify compiler version is set */
    EXPECT_GT(info.compiler_version, 0);
    
    /* Verify specific compiler detection */
#if defined(__GNUC__) && !defined(__clang__)
    EXPECT_STREQ(info.compiler_name, "GCC");
#elif defined(__clang__)
    EXPECT_STREQ(info.compiler_name, "Clang");
#elif defined(_MSC_VER)
    EXPECT_STREQ(info.compiler_name, "MSVC");
#endif
}

/*---------------------------------------------------------------------------*/
/* OS Detection Tests                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test OS detection
 */
TEST(XglPlatformTest, OSDetection) {
    xgl_platform_info_t info;
    xgl_platform_get_info(&info);
    
    /* Verify OS name is set */
    EXPECT_NE(info.os_name, nullptr);
    EXPECT_GT(strlen(info.os_name), 0);
    
    /* Verify specific OS detection */
#if defined(_WIN32) || defined(_WIN64)
    EXPECT_STREQ(info.os_name, "Windows");
#elif defined(__linux__)
    EXPECT_STREQ(info.os_name, "Linux");
#elif defined(__APPLE__)
    EXPECT_STREQ(info.os_name, "macOS");
#endif
}

/*---------------------------------------------------------------------------*/
/* Architecture Detection Tests                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test architecture detection
 */
TEST(XglPlatformTest, ArchitectureDetection) {
    xgl_platform_info_t info;
    xgl_platform_get_info(&info);
    
    /* Verify architecture name is set */
    EXPECT_NE(info.arch_name, nullptr);
    EXPECT_GT(strlen(info.arch_name), 0);
    
    /* Verify specific architecture detection */
#if defined(__x86_64__) || defined(_M_X64)
    EXPECT_STREQ(info.arch_name, "x86-64");
#elif defined(__i386__) || defined(_M_IX86)
    EXPECT_STREQ(info.arch_name, "x86");
#elif defined(__arm__) || defined(__ARM__)
    EXPECT_STREQ(info.arch_name, "ARM");
#endif
}

/*---------------------------------------------------------------------------*/
/* Integration Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test complete platform detection workflow
 */
TEST(XglPlatformTest, CompleteWorkflow) {
    /* Get platform info */
    xgl_platform_info_t info;
    xgl_platform_get_info(&info);
    
    /* Generate info string */
    char buffer[512];
    int written = xgl_platform_info_string(buffer, sizeof(buffer));
    
    /* Verify consistency */
    EXPECT_GT(written, 0);
    EXPECT_NE(strstr(buffer, info.compiler_name), nullptr);
    EXPECT_NE(strstr(buffer, info.os_name), nullptr);
    EXPECT_NE(strstr(buffer, info.arch_name), nullptr);
    
    /* Print for manual verification (optional) */
    printf("\n=== Platform Information ===\n%s", buffer);
}
