/**
 * \file            test_main.cpp
 * \brief           GoogleTest entry with explicit property-test seed replay
 * \author          X-Gen Lab
 */

#include "property/property_seed.h"

#include <cstdio>
#include <cstdlib>
#include <gmock/gmock.h>
#include <string>

class PropertySeedListener : public ::testing::EmptyTestEventListener {
  public:
    void OnTestStart(const ::testing::TestInfo&) override {
        ::testing::Test::RecordProperty("xgl_property_seed",
                                        std::to_string(property_base_seed));
        ::testing::Test::RecordProperty("xgl_property_stream",
                                        std::to_string(property_test_seed()));
    }

    void OnTestEnd(const ::testing::TestInfo& info) override {
        if (info.result()->Failed()) {
            std::fprintf(stderr,
                         "Replay: --gtest_filter=%s.%s "
                         "--xgl_property_seed=%u\n",
                         info.test_suite_name(), info.name(),
                         static_cast<unsigned int>(property_base_seed));
        }
    }
};

int main(int argc, char** argv) {
    const char* requested = nullptr;
    const char prefix[] = "--xgl_property_seed=";
    int retained = 1;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--xgl_property_seed") == 0) {
            std::fprintf(stderr,
                         "Invalid property seed: use --xgl_property_seed=N\n");
            return 2;
        }
        if (std::strncmp(argv[index], prefix, sizeof(prefix) - 1U) == 0) {
            if (requested != nullptr) {
                std::fprintf(stderr,
                             "Invalid property seed: duplicate option\n");
                return 2;
            }
            requested = argv[index] + sizeof(prefix) - 1U;
        } else {
            argv[retained++] = argv[index];
        }
    }
    argc = retained;
    argv[retained] = nullptr;
    if (requested == nullptr) {
        requested = std::getenv("XGL_PROPERTY_SEED");
    }
    if (requested != nullptr &&
        !property_parse_seed(requested, property_base_seed)) {
        std::fprintf(stderr,
                     "Invalid property seed: use decimal 0..4294967295\n");
        return 2;
    }
    ::testing::InitGoogleMock(&argc, argv);
    if (!::testing::GTEST_FLAG(list_tests)) {
        std::printf("XGL_PROPERTY_SEED=%u\n",
                    static_cast<unsigned int>(property_base_seed));
    }
    ::testing::UnitTest::GetInstance()->listeners().Append(
        new PropertySeedListener);
    return RUN_ALL_TESTS();
}
