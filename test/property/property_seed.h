/**
 * \file            property_seed.h
 * \brief           Reproducible host property-test seed configuration
 * \author          X-Gen Lab
 */

#ifndef PROPERTY_SEED_H
#define PROPERTY_SEED_H

#include <charconv>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <system_error>

inline uint32_t property_base_seed = 0x58474CU;

/**
 * \brief           Read an unsigned decimal seed without truncation
 * \param[in]       text: Nonempty decimal digits
 * \param[out]      seed: Parsed value, unchanged on failure
 * \return          True when the complete value fits in uint32_t
 */
inline bool property_parse_seed(const char* text, uint32_t& seed) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    uint32_t parsed = 0U;
    const char* end = text + std::strlen(text);
    const auto result = std::from_chars(text, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    seed = parsed;
    return true;
}

/**
 * \brief           Derive a stable stream independent of test execution order
 * \return          Base seed mixed with the currently running test name
 */
inline uint32_t property_test_seed() {
    uint32_t seed = property_base_seed;
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    if (info != nullptr) {
        for (const char* name : {info->test_suite_name(), info->name()}) {
            for (; *name != '\0'; ++name) {
                seed = (seed ^ static_cast<unsigned char>(*name)) * 16777619U;
            }
        }
    }
    return seed;
}

#endif /* PROPERTY_SEED_H */
