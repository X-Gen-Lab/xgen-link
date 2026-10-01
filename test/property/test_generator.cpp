/**
 * \file            test_generator.cpp
 * \brief           Deterministic property generator contracts
 * \author          X-Gen Lab
 */

#include "property_framework.h"

TEST(PropertyGeneratorTest, ExplicitSeedReplaysTheSameSequence) {
    PropertyTestGenerator first(713U);
    PropertyTestGenerator second(713U);
    for (size_t index = 0U; index < 100U; ++index) {
        EXPECT_EQ(first.random_uint32(), second.random_uint32());
    }
}

TEST(PropertyGeneratorTest, StandardSeedProducesKnownMt19937Vector) {
    PropertyTestGenerator generator(5489U);
    EXPECT_EQ(generator.random_uint32(), 3499211612U);
    EXPECT_EQ(generator.random_uint32(), 581869302U);
    EXPECT_EQ(generator.random_uint32(), 3890346734U);
}

TEST(PropertyGeneratorTest, DifferentSeedsProduceDifferentSequences) {
    PropertyTestGenerator first(17U);
    PropertyTestGenerator second(19U);
    EXPECT_NE(first.random_bytes(32U), second.random_bytes(32U));
}
