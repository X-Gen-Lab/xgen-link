/**
 * \file            test_fragment_properties.cpp
 * \brief           Production FRAGMENT_EXT property tests
 */

#include <gtest/gtest.h>
#include "property_framework.h"
#include <xgl/xgl_fragment.h>
#include <xgl/xgl_error.h>
#include <cstring>
#include <vector>

TEST(XglFragmentProperties, ManagerInitialization) {
    xgl_fragment_manager_t manager;
    xgl_error_t err = xgl_fragment_init(&manager, 10, 5000, nullptr);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(manager.next_message_id, 0U);

    xgl_fragment_destroy(&manager);
}

TEST(XglFragmentProperties, ManagerInitInvalidParameters) {
    EXPECT_EQ(xgl_fragment_init(nullptr, 10, 5000, nullptr), XGL_ERR_NULL_POINTER);
}

TEST(XglFragmentProperties, MessageIdAssignmentIsMonotonic32Bit) {
    xgl_fragment_manager_t manager;
    ASSERT_EQ(xgl_fragment_init(&manager, 10, 5000, nullptr), XGL_OK);

    for (uint32_t i = 0; i < 1024U; ++i) {
        EXPECT_EQ(manager.next_message_id++, i);
    }

    xgl_fragment_destroy(&manager);
}

TEST(XglFragmentProperties, FragmentExtensionReassemblyRoundTrip) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_fragment_manager_t manager;
        ASSERT_EQ(xgl_fragment_init(&manager, 10, 5000, nullptr), XGL_OK);

        size_t total_len = 2U + (gen.random_uint32() % 128U);
        std::vector<uint8_t> data = gen.random_bytes(total_len);
        size_t split = 1U + (gen.random_uint32() % (total_len - 1U));

        uint8_t* complete_data = nullptr;
        size_t complete_len = 0;
        uint32_t message_id = manager.next_message_id++;

        ASSERT_EQ(xgl_fragment_process_ext(&manager,
                                           0x1234,
                                           0xABCDEF01U,
                                           0x01020304U,
                                           7,
                                           message_id,
                                           static_cast<uint32_t>(split),
                                           static_cast<uint32_t>(total_len),
                                           data.data() + split,
                                           total_len - split,
                                           &complete_data,
                                           &complete_len,
                                           1000),
                  XGL_ERR_BUSY);

        ASSERT_EQ(xgl_fragment_process_ext(&manager,
                                           0x1234,
                                           0xABCDEF01U,
                                           0x01020304U,
                                           7,
                                           message_id,
                                           0,
                                           static_cast<uint32_t>(total_len),
                                           data.data(),
                                           split,
                                           &complete_data,
                                           &complete_len,
                                           1001),
                  XGL_OK);

        ASSERT_NE(complete_data, nullptr);
        ASSERT_EQ(complete_len, total_len);
        EXPECT_EQ(memcmp(complete_data, data.data(), total_len), 0);
        EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);

        xgl_fragment_free_data(&manager, complete_data);
        xgl_fragment_destroy(&manager);
    }
}
