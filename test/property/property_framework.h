/**
 * \file            property_framework.h
 * \brief           Property-based testing framework utilities
 * \author          Nexus Team
 */

#ifndef PROPERTY_FRAMEWORK_H
#define PROPERTY_FRAMEWORK_H

#include <gtest/gtest.h>
#include <random>
#include <vector>

/**
 * \brief           Property test configuration
 */
#define XGL_PROPERTY_TEST_ITERATIONS 100

/**
 * \brief           Random generator utilities
 */
class PropertyTestGenerator {
public:
    PropertyTestGenerator() : rng_(std::random_device{}()) {}
    
    uint8_t random_uint8() {
        return static_cast<uint8_t>(rng_() % 256);
    }
    
    uint16_t random_uint16() {
        return static_cast<uint16_t>(rng_() % 65536);
    }
    
    uint32_t random_uint32() {
        return rng_();
    }
    
    std::vector<uint8_t> random_bytes(size_t len) {
        std::vector<uint8_t> data(len);
        for (size_t i = 0; i < len; ++i) {
            data[i] = random_uint8();
        }
        return data;
    }
    
private:
    std::mt19937 rng_;
};

#endif /* PROPERTY_FRAMEWORK_H */
