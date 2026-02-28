/**
 * \file            test_fragment.cpp
 * \brief           Unit tests for fragmentation and reassembly
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_fragment.h>
#include <cstring>
#include <vector>

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglFragmentTest : public ::testing::Test {
protected:
    xgl_fragment_manager_t manager;
    
    void SetUp() override {
        /* Initialize fragment manager */
        xgl_error_t err = xgl_fragment_init(&manager, 10, 5000, nullptr);
        ASSERT_EQ(err, XGL_OK);
    }
    
    void TearDown() override {
        /* Destroy fragment manager */
        xgl_fragment_destroy(&manager);
    }
};

/*---------------------------------------------------------------------------*/
/* Basic Functionality Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST_F(XglFragmentTest, InitializeManager) {
    /* Manager should be initialized in SetUp */
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0);
}

TEST_F(XglFragmentTest, FragmentSmallData) {
    /* Create test data that needs fragmentation */
    const size_t data_len = 500;
    std::vector<uint8_t> data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    /* Fragment the data */
    const size_t max_fragment_size = 100;
    uint8_t* fragments[10];
    size_t fragment_lens[10];
    size_t fragment_count = 10;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        &manager,
        data.data(),
        data_len,
        max_fragment_size,
        fragments,
        fragment_lens,
        &fragment_count,
        &fragment_id
    );
    
    ASSERT_EQ(err, XGL_OK);
    EXPECT_GT(fragment_count, 1);
    EXPECT_LE(fragment_count, 10);
    
    /* Verify each fragment */
    for (size_t i = 0; i < fragment_count; i++) {
        EXPECT_NE(fragments[i], nullptr);
        EXPECT_GT(fragment_lens[i], 0);
        EXPECT_LE(fragment_lens[i], max_fragment_size);
    }
    
    /* Clean up */
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
}

TEST_F(XglFragmentTest, ReassembleFragments) {
    /* Create test data */
    const size_t data_len = 300;
    std::vector<uint8_t> original_data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        original_data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    /* Fragment the data */
    const size_t max_fragment_size = 80;
    uint8_t* fragments[10];
    size_t fragment_lens[10];
    size_t fragment_count = 10;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        &manager,
        original_data.data(),
        data_len,
        max_fragment_size,
        fragments,
        fragment_lens,
        &fragment_count,
        &fragment_id
    );
    
    ASSERT_EQ(err, XGL_OK);
    ASSERT_GT(fragment_count, 1);
    
    /* Process each fragment */
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    for (size_t i = 0; i < fragment_count; i++) {
        err = xgl_fragment_process(
            &manager,
            1,  /* source_id */
            0,  /* data_type */
            fragments[i],
            fragment_lens[i],
            &complete_data,
            &complete_len
        );
        
        if (i < fragment_count - 1) {
            /* Should still be waiting for more fragments */
            EXPECT_EQ(err, XGL_ERR_BUSY);
            EXPECT_EQ(complete_data, nullptr);
        } else {
            /* Last fragment should complete reassembly */
            EXPECT_EQ(err, XGL_OK);
            EXPECT_NE(complete_data, nullptr);
            EXPECT_EQ(complete_len, data_len);
        }
    }
    
    /* Verify reassembled data matches original */
    ASSERT_NE(complete_data, nullptr);
    EXPECT_EQ(memcmp(complete_data, original_data.data(), data_len), 0);
    
    /* Clean up */
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
    xgl_fragment_free_data(&manager, complete_data);
}

TEST_F(XglFragmentTest, ReassembleOutOfOrder) {
    /* Create test data */
    const size_t data_len = 200;
    std::vector<uint8_t> original_data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        original_data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    /* Fragment the data */
    const size_t max_fragment_size = 60;
    uint8_t* fragments[10];
    size_t fragment_lens[10];
    size_t fragment_count = 10;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        &manager,
        original_data.data(),
        data_len,
        max_fragment_size,
        fragments,
        fragment_lens,
        &fragment_count,
        &fragment_id
    );
    
    ASSERT_EQ(err, XGL_OK);
    ASSERT_GT(fragment_count, 1);
    
    /* Process fragments in reverse order */
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    for (int i = static_cast<int>(fragment_count) - 1; i >= 0; i--) {
        err = xgl_fragment_process(
            &manager,
            1,  /* source_id */
            0,  /* data_type */
            fragments[i],
            fragment_lens[i],
            &complete_data,
            &complete_len
        );
        
        if (i > 0) {
            /* Should still be waiting for more fragments */
            EXPECT_EQ(err, XGL_ERR_BUSY);
            EXPECT_EQ(complete_data, nullptr);
        } else {
            /* Last fragment should complete reassembly */
            EXPECT_EQ(err, XGL_OK);
            EXPECT_NE(complete_data, nullptr);
            EXPECT_EQ(complete_len, data_len);
        }
    }
    
    /* Verify reassembled data matches original */
    ASSERT_NE(complete_data, nullptr);
    EXPECT_EQ(memcmp(complete_data, original_data.data(), data_len), 0);
    
    /* Clean up */
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
    xgl_fragment_free_data(&manager, complete_data);
}

TEST_F(XglFragmentTest, DuplicateFragments) {
    /* Create test data */
    const size_t data_len = 150;
    std::vector<uint8_t> original_data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        original_data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    /* Fragment the data */
    const size_t max_fragment_size = 60;
    uint8_t* fragments[10];
    size_t fragment_lens[10];
    size_t fragment_count = 10;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        &manager,
        original_data.data(),
        data_len,
        max_fragment_size,
        fragments,
        fragment_lens,
        &fragment_count,
        &fragment_id
    );
    
    ASSERT_EQ(err, XGL_OK);
    ASSERT_GT(fragment_count, 1);
    
    /* Process first fragment */
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    err = xgl_fragment_process(
        &manager,
        1,  /* source_id */
        0,  /* data_type */
        fragments[0],
        fragment_lens[0],
        &complete_data,
        &complete_len
    );
    
    EXPECT_EQ(err, XGL_ERR_BUSY);
    
    /* Process same fragment again (duplicate) */
    err = xgl_fragment_process(
        &manager,
        1,  /* source_id */
        0,  /* data_type */
        fragments[0],
        fragment_lens[0],
        &complete_data,
        &complete_len
    );
    
    /* Should still be waiting (duplicate ignored) */
    EXPECT_EQ(err, XGL_ERR_BUSY);
    
    /* Clean up */
    xgl_fragment_clear_reassembly(&manager);
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
}

TEST_F(XglFragmentTest, MultipleFragmentSessions) {
    /* Create two different data sets */
    const size_t data_len1 = 200;
    const size_t data_len2 = 150;
    
    std::vector<uint8_t> data1(data_len1, 0xAA);
    std::vector<uint8_t> data2(data_len2, 0xBB);
    
    /* Fragment both data sets */
    const size_t max_fragment_size = 60;
    
    uint8_t* fragments1[10];
    size_t fragment_lens1[10];
    size_t fragment_count1 = 10;
    uint8_t fragment_id1;
    
    uint8_t* fragments2[10];
    size_t fragment_lens2[10];
    size_t fragment_count2 = 10;
    uint8_t fragment_id2;
    
    xgl_error_t err1 = xgl_fragment_data(
        &manager, data1.data(), data_len1, max_fragment_size,
        fragments1, fragment_lens1, &fragment_count1, &fragment_id1
    );
    
    xgl_error_t err2 = xgl_fragment_data(
        &manager, data2.data(), data_len2, max_fragment_size,
        fragments2, fragment_lens2, &fragment_count2, &fragment_id2
    );
    
    ASSERT_EQ(err1, XGL_OK);
    ASSERT_EQ(err2, XGL_OK);
    EXPECT_NE(fragment_id1, fragment_id2);
    
    /* Process fragments from both sessions interleaved */
    uint8_t* complete_data1 = nullptr;
    size_t complete_len1 = 0;
    uint8_t* complete_data2 = nullptr;
    size_t complete_len2 = 0;
    
    /* Process first fragment from each session */
    err1 = xgl_fragment_process(
        &manager, 1, 0, fragments1[0], fragment_lens1[0],
        &complete_data1, &complete_len1
    );
    EXPECT_EQ(err1, XGL_ERR_BUSY);
    
    err2 = xgl_fragment_process(
        &manager, 2, 0, fragments2[0], fragment_lens2[0],
        &complete_data2, &complete_len2
    );
    EXPECT_EQ(err2, XGL_ERR_BUSY);
    
    /* Should have 2 active reassembly buffers */
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 2);
    
    /* Clean up */
    xgl_fragment_clear_reassembly(&manager);
    xgl_fragment_free_fragments(&manager, fragments1, fragment_count1);
    xgl_fragment_free_fragments(&manager, fragments2, fragment_count2);
}

TEST_F(XglFragmentTest, InvalidParameters) {
    /* Test null manager */
    uint8_t data[100];
    uint8_t* fragments[10];
    size_t fragment_lens[10];
    size_t fragment_count = 10;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        nullptr, data, 100, 50,
        fragments, fragment_lens, &fragment_count, &fragment_id
    );
    EXPECT_NE(err, XGL_OK);
    
    /* Test null data */
    err = xgl_fragment_data(
        &manager, nullptr, 100, 50,
        fragments, fragment_lens, &fragment_count, &fragment_id
    );
    EXPECT_NE(err, XGL_OK);
    
    /* Test zero length */
    err = xgl_fragment_data(
        &manager, data, 0, 50,
        fragments, fragment_lens, &fragment_count, &fragment_id
    );
    EXPECT_NE(err, XGL_OK);
}

TEST_F(XglFragmentTest, ClearReassembly) {
    /* Create test data */
    const size_t data_len = 200;
    std::vector<uint8_t> data(data_len, 0xCC);
    
    /* Fragment the data */
    const size_t max_fragment_size = 60;
    uint8_t* fragments[10];
    size_t fragment_lens[10];
    size_t fragment_count = 10;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        &manager, data.data(), data_len, max_fragment_size,
        fragments, fragment_lens, &fragment_count, &fragment_id
    );
    
    ASSERT_EQ(err, XGL_OK);
    
    /* Process first fragment to create reassembly buffer */
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    err = xgl_fragment_process(
        &manager, 1, 0, fragments[0], fragment_lens[0],
        &complete_data, &complete_len
    );
    
    EXPECT_EQ(err, XGL_ERR_BUSY);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 1);
    
    /* Clear all reassembly buffers */
    xgl_fragment_clear_reassembly(&manager);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0);
    
    /* Clean up */
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
}

/*---------------------------------------------------------------------------*/
/* Edge Case Tests                                                           */
/*---------------------------------------------------------------------------*/

TEST_F(XglFragmentTest, SingleByteFragments) {
    /* Create small data that results in many tiny fragments */
    const size_t data_len = 50;
    std::vector<uint8_t> data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        data[i] = static_cast<uint8_t>(i);
    }
    
    /* Use very small fragment size */
    const size_t max_fragment_size = 10;  /* Will result in tiny payload per fragment */
    uint8_t* fragments[20];
    size_t fragment_lens[20];
    size_t fragment_count = 20;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        &manager, data.data(), data_len, max_fragment_size,
        fragments, fragment_lens, &fragment_count, &fragment_id
    );
    
    ASSERT_EQ(err, XGL_OK);
    
    /* Reassemble */
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    for (size_t i = 0; i < fragment_count; i++) {
        err = xgl_fragment_process(
            &manager, 1, 0, fragments[i], fragment_lens[i],
            &complete_data, &complete_len
        );
    }
    
    EXPECT_EQ(err, XGL_OK);
    ASSERT_NE(complete_data, nullptr);
    EXPECT_EQ(complete_len, data_len);
    EXPECT_EQ(memcmp(complete_data, data.data(), data_len), 0);
    
    /* Clean up */
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
    xgl_fragment_free_data(&manager, complete_data);
}

TEST_F(XglFragmentTest, LargeData) {
    /* Create large data */
    const size_t data_len = 5000;
    std::vector<uint8_t> data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    /* Fragment with reasonable size */
    const size_t max_fragment_size = 256;
    uint8_t* fragments[30];
    size_t fragment_lens[30];
    size_t fragment_count = 30;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        &manager, data.data(), data_len, max_fragment_size,
        fragments, fragment_lens, &fragment_count, &fragment_id
    );
    
    ASSERT_EQ(err, XGL_OK);
    EXPECT_GT(fragment_count, 1);
    
    /* Reassemble */
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    for (size_t i = 0; i < fragment_count; i++) {
        err = xgl_fragment_process(
            &manager, 1, 0, fragments[i], fragment_lens[i],
            &complete_data, &complete_len
        );
    }
    
    EXPECT_EQ(err, XGL_OK);
    ASSERT_NE(complete_data, nullptr);
    EXPECT_EQ(complete_len, data_len);
    EXPECT_EQ(memcmp(complete_data, data.data(), data_len), 0);
    
    /* Clean up */
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
    xgl_fragment_free_data(&manager, complete_data);
}
