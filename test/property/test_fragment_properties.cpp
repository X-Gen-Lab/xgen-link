/**
 * \file            test_fragment_properties.cpp
 * \brief           Fragmentation property tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include "property_framework.h"
#include <xgl/xgl_fragment.h>
#include <xgl/xgl_error.h>
#include <vector>

/**
 * \brief           Test fragment manager initialization
 * \details         Verifies basic initialization and cleanup
 */
TEST(XglFragmentProperties, ManagerInitialization) {
    xgl_fragment_manager_t manager;
    xgl_error_t err = xgl_fragment_init(&manager, 10, 5000, nullptr);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_fragment_destroy(&manager);
}

/**
 * \brief           Test fragment manager initialization with invalid parameters
 * \details         Verifies error handling during initialization
 */
TEST(XglFragmentProperties, ManagerInitInvalidParameters) {
    xgl_fragment_manager_t manager;
    
    /* NULL manager */
    xgl_error_t err = xgl_fragment_init(nullptr, 10, 5000, nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
    
    /* Valid initialization */
    err = xgl_fragment_init(&manager, 10, 5000, nullptr);
    EXPECT_EQ(err, XGL_OK);
    
    xgl_fragment_destroy(&manager);
}

/**
 * \brief           Test fragment ID assignment
 * \details         Verifies fragment IDs are assigned sequentially
 */
TEST(XglFragmentProperties, FragmentIDAssignment) {
    xgl_fragment_manager_t manager;
    xgl_error_t err = xgl_fragment_init(&manager, 10, 5000, nullptr);
    ASSERT_EQ(err, XGL_OK);
    
    std::vector<uint8_t> data(300, 0xAA);
    
    /* Fragment multiple times and verify IDs increment */
    for (int i = 0; i < 5; ++i) {
        uint8_t* fragments[256];
        size_t fragment_lens[256];
        size_t fragment_count = 256;
        uint8_t fragment_id = 0;
        
        err = xgl_fragment_data(&manager, data.data(), data.size(),
                               128, fragments, fragment_lens,
                               &fragment_count, &fragment_id);
        ASSERT_EQ(err, XGL_OK);
        
        EXPECT_EQ(fragment_id, (uint8_t)i)
            << "Fragment ID should increment sequentially";
        
        xgl_fragment_free_fragments(&manager, fragments, fragment_count);
    }
    
    xgl_fragment_destroy(&manager);
}
