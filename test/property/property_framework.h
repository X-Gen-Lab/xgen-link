/**
 * \file            property_framework.h
 * \brief           Property-based testing framework utilities
 * \author          Nexus Team
 */

#ifndef PROPERTY_FRAMEWORK_H
#define PROPERTY_FRAMEWORK_H

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <random>
#include <vector>
#include <algorithm>

/**
 * \brief           Property test configuration
 */
#define XGL_PROPERTY_TEST_ITERATIONS 100

/**
 * \brief           Random generator utilities for property-based testing
 */
class PropertyTestGenerator {
public:
    PropertyTestGenerator() : rng_(std::random_device{}()) {}
    
    /**
     * \brief           Generate random uint8_t
     */
    uint8_t random_uint8() {
        return static_cast<uint8_t>(rng_() % 256);
    }
    
    /**
     * \brief           Generate random uint8_t in range
     */
    uint8_t random_uint8(uint8_t min, uint8_t max) {
        return min + (rng_() % (max - min + 1));
    }
    
    /**
     * \brief           Generate random uint16_t
     */
    uint16_t random_uint16() {
        return static_cast<uint16_t>(rng_() % 65536);
    }
    
    /**
     * \brief           Generate random uint16_t in range
     */
    uint16_t random_uint16(uint16_t min, uint16_t max) {
        return min + (rng_() % (max - min + 1));
    }
    
    /**
     * \brief           Generate random uint32_t
     */
    uint32_t random_uint32() {
        return rng_();
    }
    
    /**
     * \brief           Generate random uint32_t in range
     */
    uint32_t random_uint32(uint32_t min, uint32_t max) {
        if (max <= min) return min;
        return min + (rng_() % (max - min + 1));
    }
    
    /**
     * \brief           Generate random byte array
     */
    std::vector<uint8_t> random_bytes(size_t len) {
        std::vector<uint8_t> data(len);
        for (size_t i = 0; i < len; ++i) {
            data[i] = random_uint8();
        }
        return data;
    }
    
    /**
     * \brief           Generate random byte array with length in range
     */
    std::vector<uint8_t> random_bytes(size_t min_len, size_t max_len) {
        size_t len = static_cast<size_t>(random_uint32(
            static_cast<uint32_t>(min_len), 
            static_cast<uint32_t>(max_len)
        ));
        return random_bytes(len);
    }
    
    /**
     * \brief           Generate random boolean
     */
    bool random_bool() {
        return (rng_() % 2) == 0;
    }
    
    /**
     * \brief           Generate random data type (0-15)
     */
    uint8_t random_data_type() {
        return random_uint8(0, 15);
    }
    
    /**
     * \brief           Generate random priority (0-7)
     */
    uint8_t random_priority() {
        return random_uint8(0, 7);
    }
    
    /**
     * \brief           Generate random node ID (1-254, avoiding 0 and 255)
     */
    uint8_t random_node_id() {
        return random_uint8(1, 254);
    }
    
    uint32_t random_packet_number() {
        return random_uint32();
    }
    
    /**
     * \brief           Corrupt random byte in data
     */
    void corrupt_random_byte(std::vector<uint8_t>& data) {
        if (data.empty()) return;
        size_t index = rng_() % data.size();
        data[index] ^= 0xFF;  /* Flip all bits */
    }
    
    /**
     * \brief           Corrupt random bit in data
     */
    void corrupt_random_bit(std::vector<uint8_t>& data) {
        if (data.empty()) return;
        size_t byte_index = rng_() % data.size();
        uint8_t bit_index = rng_() % 8;
        data[byte_index] ^= (1 << bit_index);
    }
    
    /**
     * \brief           Generate random frame header
     */
    struct RandomFrameHeader {
        uint8_t version;
        uint8_t packet_type;
        uint8_t flags;
        uint16_t source_id;
        uint16_t target_id;
        uint32_t connection_id;
        uint32_t packet_number;
        uint8_t traffic_class;
        uint16_t data_len;
    };
    
    RandomFrameHeader random_frame_header() {
        RandomFrameHeader header;
        header.version = XGL_WIRE_VERSION;
        header.packet_type = random_data_type();
        header.flags = random_uint8();
        header.source_id = random_node_id();
        header.target_id = random_node_id();
        header.connection_id = random_uint32();
        header.packet_number = random_packet_number();
        header.traffic_class = random_priority();
        header.data_len = random_uint16(0, 1024);
        return header;
    }
    
    /**
     * \brief           Generate random packet data
     */
    std::vector<uint8_t> random_packet_data(size_t max_size = 1024) {
        size_t len = static_cast<size_t>(random_uint32(1, static_cast<uint32_t>(max_size)));
        return random_bytes(len);
    }
    
    /**
     * \brief           Shuffle vector elements
     */
    template<typename T>
    void shuffle(std::vector<T>& vec) {
        std::shuffle(vec.begin(), vec.end(), rng_);
    }
    
private:
    std::mt19937 rng_;
};

/**
 * \brief           Property test helper macros
 */
#define PROPERTY_TEST(test_suite, test_name) \
    TEST(test_suite, test_name)

#define FOR_ALL_ITERATIONS(iterations) \
    for (int _iteration = 0; _iteration < (iterations); ++_iteration)

#define PROPERTY_ASSERT(condition) \
    ASSERT_TRUE(condition) << "Property violated at iteration " << _iteration

#define PROPERTY_EXPECT(condition) \
    EXPECT_TRUE(condition) << "Property violated at iteration " << _iteration

#endif /* PROPERTY_FRAMEWORK_H */
