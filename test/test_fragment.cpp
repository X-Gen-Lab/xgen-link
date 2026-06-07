/**
 * \file            test_fragment.cpp
 * \brief           Unit tests for fragmentation and reassembly
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl_fragment.h>
#include <xgl/xgl_wire.h>
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

TEST_F(XglFragmentTest, FragmentHeaderUsesExplicitWireFormat) {
    std::vector<uint8_t> data(300);
    for (size_t i = 0; i < data.size(); i++) {
        data[i] = static_cast<uint8_t>(i & 0xFFU);
    }

    uint8_t* fragments[8] = {};
    size_t fragment_lens[8] = {};
    size_t fragment_count = 8;
    uint8_t fragment_id = 0;

    ASSERT_EQ(xgl_fragment_data(&manager,
                                data.data(),
                                data.size(),
                                80,
                                fragments,
                                fragment_lens,
                                &fragment_count,
                                &fragment_id),
              XGL_OK);
    ASSERT_GT(fragment_count, 1U);
    ASSERT_GE(fragment_lens[0], static_cast<size_t>(XGL_FRAGMENT_HEADER_SIZE));

    EXPECT_EQ(XGL_FRAGMENT_HEADER_SIZE, 5U);
    EXPECT_EQ(fragments[0][0], fragment_id);
    EXPECT_EQ(fragments[0][1], 0U);
    EXPECT_EQ(fragments[0][2], static_cast<uint8_t>(fragment_count));
    EXPECT_EQ(fragments[0][3], 0U);
    EXPECT_EQ(fragments[0][4], 0U);

    const size_t first_payload_size = fragment_lens[0] - XGL_FRAGMENT_HEADER_SIZE;
    ASSERT_GT(first_payload_size, 0U);
    EXPECT_EQ(std::memcmp(fragments[0] + XGL_FRAGMENT_HEADER_SIZE,
                          data.data(),
                          first_payload_size),
              0);

    const size_t second_offset = first_payload_size;
    EXPECT_EQ(fragments[1][1], 1U);
    EXPECT_EQ(fragments[1][3], static_cast<uint8_t>(second_offset & 0xFFU));
    EXPECT_EQ(fragments[1][4], static_cast<uint8_t>((second_offset >> 8U) & 0xFFU));

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
    
    /* Process each fragment for reassembly */
    uint8_t source_id = 1;
    uint8_t data_type = 5;
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    for (size_t i = 0; i < fragment_count; i++) {
        err = xgl_fragment_process(
            &manager,
            source_id,
            data_type,
            fragments[i],
            fragment_lens[i],
            &complete_data,
            &complete_len,
            0
        );
        
        if (i < fragment_count - 1) {
            /* Should still be waiting for more fragments */
            EXPECT_EQ(err, XGL_ERR_BUSY);
            EXPECT_EQ(complete_data, nullptr);
        } else {
            /* Last fragment - reassembly should be complete */
            EXPECT_EQ(err, XGL_OK);
            ASSERT_NE(complete_data, nullptr);
            EXPECT_EQ(complete_len, data_len);
            
            /* Verify data integrity */
            EXPECT_EQ(memcmp(complete_data, original_data.data(), data_len), 0);
        }
    }
    
    /* Clean up */
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
    if (complete_data != nullptr) {
        xgl_fragment_free_data(&manager, complete_data);
    }
}

TEST_F(XglFragmentTest, ReassembleFragmentsFromFragmentExtensionMetadata) {
    const std::vector<uint8_t> part0 = {'a', 'b', 'c', 'd'};
    const std::vector<uint8_t> part1 = {'e', 'f', 'g'};
    const uint32_t message_id = 0x01020304U;
    const uint32_t message_len = static_cast<uint32_t>(part0.size() + part1.size());

    uint8_t ext_value[16] = {};
    size_t ext_value_len = 0;
    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(ext_value,
                                                 sizeof(ext_value),
                                                 message_id,
                                                 static_cast<uint32_t>(part0.size()),
                                                 message_len,
                                                 &ext_value_len),
              XGL_OK);

    uint32_t decoded_message_id = 0;
    uint32_t decoded_offset = 0;
    uint32_t decoded_message_len = 0;
    ASSERT_EQ(xgl_wire_decode_fragment_ext_value(ext_value,
                                                 ext_value_len,
                                                 &decoded_message_id,
                                                 &decoded_offset,
                                                 &decoded_message_len),
              XGL_OK);

    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    EXPECT_EQ(xgl_fragment_process_ext(&manager,
                                       0x1234,
                                       0xAABBCCDDU,
                                       0x11223344U,
                                       2,
                                       decoded_message_id,
                                       decoded_offset,
                                       decoded_message_len,
                                       part1.data(),
                                       part1.size(),
                                       &complete_data,
                                       &complete_len,
                                       1000),
              XGL_ERR_BUSY);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 1U);

    EXPECT_EQ(xgl_fragment_process_ext(&manager,
                                       0x1234,
                                       0xAABBCCDDU,
                                       0x11223344U,
                                       2,
                                       message_id,
                                       0,
                                       message_len,
                                       part0.data(),
                                       part0.size(),
                                       &complete_data,
                                       &complete_len,
                                       1001),
              XGL_OK);

    ASSERT_NE(complete_data, nullptr);
    ASSERT_EQ(complete_len, message_len);
    EXPECT_EQ(std::memcmp(complete_data, "abcdefg", message_len), 0);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);

    xgl_fragment_free_data(&manager, complete_data);
}

TEST_F(XglFragmentTest, FragmentExtensionKeyIncludesSession) {
    const uint8_t part[] = {'x', 'y'};
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;

    EXPECT_EQ(xgl_fragment_process_ext(&manager,
                                       0x1234,
                                       1,
                                       100,
                                       2,
                                       77,
                                       0,
                                       4,
                                       part,
                                       sizeof(part),
                                       &complete_data,
                                       &complete_len,
                                       1000),
              XGL_ERR_BUSY);
    EXPECT_EQ(xgl_fragment_process_ext(&manager,
                                       0x1234,
                                       1,
                                       101,
                                       2,
                                       77,
                                       0,
                                       4,
                                       part,
                                       sizeof(part),
                                       &complete_data,
                                       &complete_len,
                                       1001),
              XGL_ERR_BUSY);

    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 2U);
}

TEST_F(XglFragmentTest, ClearFragmentExtensionReassemblyIsScopedToConnectionSession) {
    const uint8_t part[] = {'x', 'y'};
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;

    ASSERT_EQ(xgl_fragment_process_ext(&manager,
                                       0x1234,
                                       1,
                                       100,
                                       2,
                                       77,
                                       0,
                                       4,
                                       part,
                                       sizeof(part),
                                       &complete_data,
                                       &complete_len,
                                       1000),
              XGL_ERR_BUSY);
    ASSERT_EQ(xgl_fragment_process_ext(&manager,
                                       0x1234,
                                       2,
                                       200,
                                       2,
                                       88,
                                       0,
                                       4,
                                       part,
                                       sizeof(part),
                                       &complete_data,
                                       &complete_len,
                                       1001),
              XGL_ERR_BUSY);
    ASSERT_EQ(xgl_fragment_get_reassembly_count(&manager), 2U);

    EXPECT_EQ(xgl_fragment_clear_reassembly_scope(&manager,
                                                 0x1234,
                                                 1,
                                                 100),
              1U);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 1U);

    const uint8_t tail[] = {'z', 'w'};
    EXPECT_EQ(xgl_fragment_process_ext(&manager,
                                       0x1234,
                                       2,
                                       200,
                                       2,
                                       88,
                                       2,
                                       4,
                                       tail,
                                       sizeof(tail),
                                       &complete_data,
                                       &complete_len,
                                       1002),
              XGL_OK);
    ASSERT_NE(complete_data, nullptr);
    ASSERT_EQ(complete_len, 4U);
    EXPECT_EQ(std::memcmp(complete_data, "xyzw", 4U), 0);
    xgl_fragment_free_data(&manager, complete_data);
}

TEST_F(XglFragmentTest, RejectsFragmentOffsetThatDoesNotMatchIndexOrder) {
    uint8_t first_fragment[] = {
        7, 0, 3, 0, 0,
        'a', 'b', 'c', 'd'
    };
    uint8_t invalid_second_fragment[] = {
        7, 1, 3, 100, 0,
        'e', 'f', 'g', 'h'
    };

    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;

    EXPECT_EQ(xgl_fragment_process(&manager,
                                   1,
                                   2,
                                   first_fragment,
                                   sizeof(first_fragment),
                                   &complete_data,
                                   &complete_len,
                                   1000),
              XGL_ERR_BUSY);

    EXPECT_EQ(xgl_fragment_process(&manager,
                                   1,
                                   2,
                                   invalid_second_fragment,
                                   sizeof(invalid_second_fragment),
                                   &complete_data,
                                   &complete_len,
                                   1001),
              XGL_ERR_INVALID_FRAME);
}

TEST_F(XglFragmentTest, RejectsReassemblyExceedingMaxMessageSize) {
    ASSERT_EQ(xgl_fragment_set_limits(&manager, 12, 0), XGL_OK);

    uint8_t first_fragment[] = {
        9, 0, 3, 0, 0,
        'a', 'b', 'c', 'd', 'e', 'f'
    };

    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;

    EXPECT_EQ(xgl_fragment_process(&manager,
                                   1,
                                   2,
                                   first_fragment,
                                   sizeof(first_fragment),
                                   &complete_data,
                                   &complete_len,
                                   1000),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);
}

TEST_F(XglFragmentTest, EnforcesAggregateReassemblyByteBudget) {
    ASSERT_EQ(xgl_fragment_set_limits(&manager, 0, 16), XGL_OK);

    uint8_t first_message[] = {
        11, 0, 2, 0, 0,
        'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'
    };
    uint8_t second_message[] = {
        12, 0, 2, 0, 0,
        'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p'
    };

    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;

    EXPECT_EQ(xgl_fragment_process(&manager,
                                   1,
                                   2,
                                   first_message,
                                   sizeof(first_message),
                                   &complete_data,
                                   &complete_len,
                                   1000),
              XGL_ERR_BUSY);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 1U);

    EXPECT_EQ(xgl_fragment_process(&manager,
                                   2,
                                   2,
                                   second_message,
                                   sizeof(second_message),
                                   &complete_data,
                                   &complete_len,
                                   1001),
              XGL_ERR_NO_MEMORY);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 1U);
}

TEST_F(XglFragmentTest, LargeData) {
    /* Create large test data */
    const size_t data_len = 2000;
    std::vector<uint8_t> data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    /* Fragment the data */
    const size_t max_fragment_size = 128;
    uint8_t* fragments[256];
    size_t fragment_lens[256];
    size_t fragment_count = 256;
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
    
    /* Reassemble */
    uint8_t source_id = 2;
    uint8_t data_type = 10;
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    for (size_t i = 0; i < fragment_count; i++) {
        err = xgl_fragment_process(
            &manager,
            source_id,
            data_type,
            fragments[i],
            fragment_lens[i],
            &complete_data,
            &complete_len,
            0
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

TEST_F(XglFragmentTest, ReassembleStressReverseOrderLargePayload) {
    const size_t data_len = 4096;
    const size_t max_fragment_size = 64;
    std::vector<uint8_t> data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        data[i] = static_cast<uint8_t>((i * 31U) & 0xFFU);
    }

    uint8_t* fragments[128];
    size_t fragment_lens[128];
    size_t fragment_count = 128;
    uint8_t fragment_id;

    xgl_error_t err = xgl_fragment_data(&manager,
                                        data.data(),
                                        data.size(),
                                        max_fragment_size,
                                        fragments,
                                        fragment_lens,
                                        &fragment_count,
                                        &fragment_id);
    ASSERT_EQ(err, XGL_OK);
    ASSERT_GT(fragment_count, 32U);

    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;

    for (size_t i = fragment_count; i > 0; i--) {
        size_t index = i - 1U;
        err = xgl_fragment_process(&manager,
                                   3,
                                   9,
                                   fragments[index],
                                   fragment_lens[index],
                                   &complete_data,
                                   &complete_len,
                                   1000);
        if (index > 0U) {
            EXPECT_EQ(err, XGL_ERR_BUSY);
            EXPECT_EQ(complete_data, nullptr);
        }
    }

    EXPECT_EQ(err, XGL_OK);
    ASSERT_NE(complete_data, nullptr);
    EXPECT_EQ(complete_len, data_len);
    EXPECT_EQ(memcmp(complete_data, data.data(), data_len), 0);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);

    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
    xgl_fragment_free_data(&manager, complete_data);
}

TEST_F(XglFragmentTest, InvalidParameters) {
    std::vector<uint8_t> data(100, 0xAA);
    uint8_t* fragments[10];
    size_t fragment_lens[10];
    size_t fragment_count = 10;
    uint8_t fragment_id;
    
    /* NULL manager */
    xgl_error_t err = xgl_fragment_data(
        nullptr,
        data.data(),
        data.size(),
        50,
        fragments,
        fragment_lens,
        &fragment_count,
        &fragment_id
    );
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
    
    /* NULL data */
    err = xgl_fragment_data(
        &manager,
        nullptr,
        data.size(),
        50,
        fragments,
        fragment_lens,
        &fragment_count,
        &fragment_id
    );
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
    
    /* Zero data length */
    err = xgl_fragment_data(
        &manager,
        data.data(),
        0,
        50,
        fragments,
        fragment_lens,
        &fragment_count,
        &fragment_id
    );
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglFragmentTest, TimeoutHandling) {
    /* Create test data */
    const size_t data_len = 200;
    std::vector<uint8_t> data(data_len);
    for (size_t i = 0; i < data_len; i++) {
        data[i] = static_cast<uint8_t>(i);
    }
    
    /* Fragment the data */
    uint8_t* fragments[10];
    size_t fragment_lens[10];
    size_t fragment_count = 10;
    uint8_t fragment_id;
    
    xgl_error_t err = xgl_fragment_data(
        &manager,
        data.data(),
        data_len,
        50,
        fragments,
        fragment_lens,
        &fragment_count,
        &fragment_id
    );
    ASSERT_EQ(err, XGL_OK);
    
    /* Process only first fragment with mock time = 1000ms */
    uint8_t* complete_data = nullptr;
    size_t complete_len = 0;
    
    err = xgl_fragment_process(
        &manager,
        1,
        1,
        fragments[0],
        fragment_lens[0],
        &complete_data,
        &complete_len,
        1000  /* Start time */
    );
    EXPECT_EQ(err, XGL_ERR_BUSY);
    
    /* Process timeouts at time 7000ms (6000ms elapsed, exceeds 5000ms timeout) */
    uint32_t timeout_count = xgl_fragment_process_timeouts(&manager, 7000);
    EXPECT_GT(timeout_count, 0);
    
    /* Clean up */
    xgl_fragment_free_fragments(&manager, fragments, fragment_count);
}
