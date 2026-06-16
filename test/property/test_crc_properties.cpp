/**
 * \file            test_crc_properties.cpp
 * \brief           CRC calculation property tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_crc.h>
#include "property_framework.h"

/*---------------------------------------------------------------------------*/
/* Reference CRC Implementations (for validation)                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Reference CRC8-MAXIM implementation (bit-by-bit, reflected)
 * \details         Used to validate the lookup table implementation
 *                  Uses reflected algorithm (LSB first) to match lookup table
 */
static uint8_t reference_crc8_maxim(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    const uint8_t polynomial = 0x8C;  /* Reflected 0x31 */

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x01) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc = (crc >> 1);
            }
        }
    }

    return crc;
}

/**
 * \brief           Reference CRC16-MODBUS implementation (bit-by-bit)
 * \details         Used to validate the lookup table implementation
 */
static uint16_t reference_crc16_modbus(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    const uint16_t polynomial = 0xA001;  /* Reversed 0x8005 */

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc = (crc >> 1);
            }
        }
    }

    return crc;
}

/*---------------------------------------------------------------------------*/
/* Property 4: CRC Calculation Correctness                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 4: CRC Calculation Correctness
 * \details         For any data buffer, the CRC8 (MAXIM) and CRC16 (MODBUS)
 *                  calculations should match reference implementations.
 *                  Validates: Requirements 13.1, 13.2, 13.3
 */
TEST(XglCrcProperties, CrcCalculationCorrectness) {
    PropertyTestGenerator gen;

    /* Test with 100+ random inputs of varying lengths */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random data length (0 to 1024 bytes) */
        size_t len = gen.random_uint32() % 1025;

        /* Generate random data */
        std::vector<uint8_t> data = gen.random_bytes(len);

        /* Test CRC8-MAXIM */
        uint8_t crc8_impl = xgl_crc8_maxim(data.data(), len);
        uint8_t crc8_ref = reference_crc8_maxim(data.data(), len);

        EXPECT_EQ(crc8_impl, crc8_ref)
            << "CRC8-MAXIM mismatch at iteration " << iteration
            << " with data length " << len;

        /* Test CRC16-MODBUS */
        uint16_t crc16_impl = xgl_crc16_modbus(data.data(), len);
        uint16_t crc16_ref = reference_crc16_modbus(data.data(), len);

        EXPECT_EQ(crc16_impl, crc16_ref)
            << "CRC16-MODBUS mismatch at iteration " << iteration
            << " with data length " << len;
    }
}

/**
 * \brief           Property: CRC8 incremental calculation matches full calculation
 * \details         For any data buffer split at any point, calculating CRC8
 *                  incrementally should match calculating it in one pass.
 */
TEST(XglCrcProperties, Crc8IncrementalConsistency) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random data (at least 2 bytes for splitting) */
        size_t len = 2 + (gen.random_uint32() % 1023);
        std::vector<uint8_t> data = gen.random_bytes(len);

        /* Calculate CRC8 in one pass */
        uint8_t crc_full = xgl_crc8_maxim(data.data(), len);

        /* Calculate CRC8 incrementally at random split point */
        size_t split = 1 + (gen.random_uint32() % (len - 1));
        uint8_t crc_inc = xgl_crc8_maxim_update(0x00, data.data(), split);
        crc_inc = xgl_crc8_maxim_update(crc_inc, data.data() + split, len - split);

        EXPECT_EQ(crc_full, crc_inc)
            << "CRC8 incremental mismatch at iteration " << iteration
            << " with split at " << split << "/" << len;
    }
}

/**
 * \brief           Property: CRC16 incremental calculation matches full calculation
 * \details         For any data buffer split at any point, calculating CRC16
 *                  incrementally should match calculating it in one pass.
 */
TEST(XglCrcProperties, Crc16IncrementalConsistency) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random data (at least 2 bytes for splitting) */
        size_t len = 2 + (gen.random_uint32() % 1023);
        std::vector<uint8_t> data = gen.random_bytes(len);

        /* Calculate CRC16 in one pass */
        uint16_t crc_full = xgl_crc16_modbus(data.data(), len);

        /* Calculate CRC16 incrementally at random split point */
        size_t split = 1 + (gen.random_uint32() % (len - 1));
        uint16_t crc_inc = xgl_crc16_modbus_update(0xFFFF, data.data(), split);
        crc_inc = xgl_crc16_modbus_update(crc_inc, data.data() + split, len - split);

        EXPECT_EQ(crc_full, crc_inc)
            << "CRC16 incremental mismatch at iteration " << iteration
            << " with split at " << split << "/" << len;
    }
}

/**
 * \brief           Property: Different data produces different CRCs (collision resistance)
 * \details         For any two different data buffers of the same length,
 *                  the CRC values should be different (with high probability).
 */
TEST(XglCrcProperties, CrcCollisionResistance) {
    PropertyTestGenerator gen;
    int crc8_collisions = 0;
    int crc16_collisions = 0;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate two different random data buffers */
        size_t len = 1 + (gen.random_uint32() % 256);
        std::vector<uint8_t> data1 = gen.random_bytes(len);
        std::vector<uint8_t> data2 = gen.random_bytes(len);

        /* Ensure they're different */
        if (data1 == data2) {
            data2[0] ^= 0x01;  /* Flip one bit */
        }

        /* Calculate CRCs */
        uint8_t crc8_1 = xgl_crc8_maxim(data1.data(), len);
        uint8_t crc8_2 = xgl_crc8_maxim(data2.data(), len);

        uint16_t crc16_1 = xgl_crc16_modbus(data1.data(), len);
        uint16_t crc16_2 = xgl_crc16_modbus(data2.data(), len);

        /* Count collisions */
        if (crc8_1 == crc8_2) {
            crc8_collisions++;
        }
        if (crc16_1 == crc16_2) {
            crc16_collisions++;
        }
    }

    /* CRC8 has 256 possible values, expect some collisions but not too many */
    EXPECT_LT(crc8_collisions, XGL_PROPERTY_TEST_ITERATIONS / 2)
        << "Too many CRC8 collisions: " << crc8_collisions;

    /* CRC16 has 65536 possible values, expect very few collisions */
    EXPECT_LT(crc16_collisions, XGL_PROPERTY_TEST_ITERATIONS / 10)
        << "Too many CRC16 collisions: " << crc16_collisions;
}

/**
 * \brief           Property: Single bit flip changes CRC (error detection)
 * \details         For any data buffer, flipping any single bit should
 *                  produce a different CRC value.
 */
TEST(XglCrcProperties, CrcSingleBitFlipDetection) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random data */
        size_t len = 1 + (gen.random_uint32() % 256);
        std::vector<uint8_t> data = gen.random_bytes(len);

        /* Calculate original CRCs */
        uint8_t crc8_orig = xgl_crc8_maxim(data.data(), len);
        uint16_t crc16_orig = xgl_crc16_modbus(data.data(), len);

        /* Flip a random bit */
        size_t byte_pos = gen.random_uint32() % len;
        uint8_t bit_pos = gen.random_uint8() % 8;
        data[byte_pos] ^= (1 << bit_pos);

        /* Calculate new CRCs */
        uint8_t crc8_new = xgl_crc8_maxim(data.data(), len);
        uint16_t crc16_new = xgl_crc16_modbus(data.data(), len);

        /* CRCs should be different */
        EXPECT_NE(crc8_orig, crc8_new)
            << "CRC8 failed to detect single bit flip at iteration " << iteration
            << " byte " << byte_pos << " bit " << (int)bit_pos;

        EXPECT_NE(crc16_orig, crc16_new)
            << "CRC16 failed to detect single bit flip at iteration " << iteration
            << " byte " << byte_pos << " bit " << (int)bit_pos;
    }
}
