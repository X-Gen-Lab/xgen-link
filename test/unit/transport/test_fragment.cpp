#include <xgen/memory/libc_allocator.h>
/**
 * \file            test_fragment.cpp
 * \brief           Production FRAGMENT_EXT reassembly tests
 */

#include <transport/xgl_fragment.h>
#include <wire/xgl_wire.h>

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

class XglFragmentTest : public ::testing::Test {
  protected:
    xgl_fragment_manager_t manager;

    void SetUp() override {
        ASSERT_EQ(xgl_fragment_init(&manager, 10, 5000, xgm_allocator_libc()),
                  XGL_OK);
    }

    void TearDown() override {
        xgl_fragment_destroy(&manager);
    }
};

TEST_F(XglFragmentTest, InitializeManager) {
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);
    EXPECT_EQ(manager.current_reassembly_bytes, 0U);
}

TEST_F(XglFragmentTest, ReassembleFragmentsFromFragmentExtensionMetadata) {
    const std::vector<uint8_t> part0 = {'a', 'b', 'c', 'd'};
    const std::vector<uint8_t> part1 = {'e', 'f', 'g'};
    const uint32_t message_id = 0x01020304U;
    const uint32_t message_len =
        static_cast<uint32_t>(part0.size() + part1.size());

    uint8_t ext_value[16] = {};
    size_t ext_value_len = 0;
    ASSERT_EQ(xgl_wire_encode_fragment_ext_value(
                  ext_value, sizeof(ext_value), message_id,
                  static_cast<uint32_t>(part0.size()), message_len,
                  &ext_value_len),
              XGL_OK);

    uint32_t decoded_message_id = 0;
    uint32_t decoded_offset = 0;
    uint32_t decoded_message_len = 0;
    ASSERT_EQ(xgl_wire_decode_fragment_ext_value(
                  ext_value, ext_value_len, &decoded_message_id,
                  &decoded_offset, &decoded_message_len),
              XGL_OK);

    xgl_fragment_message_t complete = {};
    EXPECT_EQ(xgl_fragment_process_ext(
                  &manager, 0x1234, 0xAABBCCDDU, 0x11223344U, 2,
                  decoded_message_id, decoded_offset, decoded_message_len,
                  part1.data(), part1.size(), &complete, 1000),
              XGL_ERR_BUSY);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 1U);

    EXPECT_EQ(xgl_fragment_process_ext(
                  &manager, 0x1234, 0xAABBCCDDU, 0x11223344U, 2, message_id, 0,
                  message_len, part0.data(), part0.size(), &complete, 1001),
              XGL_OK);

    ASSERT_NE(complete.data, nullptr);
    ASSERT_EQ(complete.len, message_len);
    EXPECT_EQ(std::memcmp(complete.data, "abcdefg", message_len), 0);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);

    xgl_fragment_release_message(&manager, &complete);
}

TEST_F(XglFragmentTest, FragmentExtensionKeyIncludesSession) {
    const uint8_t part[] = {'x', 'y'};
    xgl_fragment_message_t complete = {};

    EXPECT_EQ(xgl_fragment_process_ext(&manager, 0x1234, 1, 100, 2, 77, 0, 4,
                                       part, sizeof(part), &complete, 1000),
              XGL_ERR_BUSY);
    EXPECT_EQ(xgl_fragment_process_ext(&manager, 0x1234, 1, 101, 2, 77, 0, 4,
                                       part, sizeof(part), &complete, 1001),
              XGL_ERR_BUSY);

    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 2U);
}

TEST_F(XglFragmentTest,
       ClearFragmentExtensionReassemblyIsScopedToConnectionSession) {
    const uint8_t part[] = {'x', 'y'};
    xgl_fragment_message_t complete = {};

    ASSERT_EQ(xgl_fragment_process_ext(&manager, 0x1234, 1, 100, 2, 77, 0, 4,
                                       part, sizeof(part), &complete, 1000),
              XGL_ERR_BUSY);
    ASSERT_EQ(xgl_fragment_process_ext(&manager, 0x1234, 2, 200, 2, 88, 0, 4,
                                       part, sizeof(part), &complete, 1001),
              XGL_ERR_BUSY);
    ASSERT_EQ(xgl_fragment_get_reassembly_count(&manager), 2U);

    EXPECT_EQ(xgl_fragment_clear_reassembly_scope(&manager, 0x1234, 1, 100),
              1U);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 1U);

    const uint8_t tail[] = {'z', 'w'};
    EXPECT_EQ(xgl_fragment_process_ext(&manager, 0x1234, 2, 200, 2, 88, 2, 4,
                                       tail, sizeof(tail), &complete, 1002),
              XGL_OK);
    ASSERT_NE(complete.data, nullptr);
    ASSERT_EQ(complete.len, 4U);
    EXPECT_EQ(std::memcmp(complete.data, "xyzw", 4U), 0);
    xgl_fragment_release_message(&manager, &complete);
}

TEST_F(XglFragmentTest, RejectsDuplicateByteRange) {
    const uint8_t part[] = {'x', 'y'};
    xgl_fragment_message_t complete = {};

    ASSERT_EQ(xgl_fragment_process_ext(&manager, 1, 1, 1, 1, 1, 0, 4, part,
                                       sizeof(part), &complete, 1000),
              XGL_ERR_BUSY);

    EXPECT_EQ(xgl_fragment_process_ext(&manager, 1, 1, 1, 1, 1, 0, 4, part,
                                       sizeof(part), &complete, 1001),
              XGL_ERR_BUSY);
}

TEST_F(XglFragmentTest, RejectsReassemblyExceedingMaxMessageSize) {
    ASSERT_EQ(xgl_fragment_set_limits(&manager, 12, 0), XGL_OK);

    const uint8_t part[] = {'a', 'b'};
    xgl_fragment_message_t complete = {};

    EXPECT_EQ(xgl_fragment_process_ext(&manager, 1, 1, 1, 1, 1, 0, 13, part,
                                       sizeof(part), &complete, 1000),
              XGL_ERR_BUFFER_TOO_SMALL);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);
}

TEST_F(XglFragmentTest, EnforcesAggregateReassemblyByteBudget) {
    ASSERT_EQ(xgl_fragment_set_limits(&manager, 0, 16), XGL_OK);

    const uint8_t part[] = {'a', 'b'};
    xgl_fragment_message_t complete = {};

    EXPECT_EQ(xgl_fragment_process_ext(&manager, 1, 1, 1, 1, 1, 0, 16, part,
                                       sizeof(part), &complete, 1000),
              XGL_ERR_BUSY);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 1U);

    EXPECT_EQ(xgl_fragment_process_ext(&manager, 2, 2, 2, 1, 2, 0, 1, part, 1,
                                       &complete, 1001),
              XGL_ERR_NO_MEMORY);
}

TEST_F(XglFragmentTest, CompletedPayloadRetainsBudgetUntilRelease) {
    ASSERT_EQ(xgl_fragment_set_limits(&manager, 2, 2), XGL_OK);
    const uint8_t payload[] = {'a', 'b'};
    xgl_fragment_message_t complete = {};
    ASSERT_EQ(xgl_fragment_process_ext(&manager, 1, 1, 1, 1, 1, 0, 2, payload,
                                       sizeof(payload), &complete, 1000),
              XGL_OK);
    EXPECT_EQ(manager.current_reassembly_bytes, 2U);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);

    xgl_fragment_message_t other = {};
    EXPECT_EQ(xgl_fragment_process_ext(&manager, 2, 2, 2, 1, 2, 0, 2, payload,
                                       1, &other, 1001),
              XGL_ERR_NO_MEMORY);
    xgl_fragment_release_message(&manager, &complete);
    EXPECT_EQ(manager.current_reassembly_bytes, 0U);
    EXPECT_EQ(complete.data, nullptr);
    EXPECT_EQ(complete.len, 0U);
    xgl_fragment_release_message(&manager, &complete);
    EXPECT_EQ(manager.current_reassembly_bytes, 0U);
    ASSERT_EQ(xgl_fragment_process_ext(&manager, 2, 2, 2, 1, 2, 0, 2, payload,
                                       sizeof(payload), &other, 1002),
              XGL_OK);
    xgl_fragment_release_message(&manager, &other);
    EXPECT_EQ(manager.current_reassembly_bytes, 0U);
}

TEST_F(XglFragmentTest, ReassemblyTracksReceivedRangesInsteadOfByteBitmap) {
    const std::vector<uint8_t> part(64, 0x5A);
    xgl_fragment_message_t complete = {};

    ASSERT_EQ(xgl_fragment_process_ext(
                  &manager, 0x1234, 0xAABBCCDDU, 0x11223344U, 2, 0x01020304U,
                  128, 1024, part.data(), part.size(), &complete, 1000),
              XGL_ERR_BUSY);

    ASSERT_NE(manager.reassembly_list.head, nullptr);
    xgl_reassembly_buffer_t* buffer = XGCT_LIST_ENTRY(
        manager.reassembly_list.head, xgl_reassembly_buffer_t, node);

    ASSERT_EQ(buffer->received_range_count, 1U);
    EXPECT_EQ(buffer->received_ranges[0].start, 128U);
    EXPECT_EQ(buffer->received_ranges[0].end, 192U);
}

TEST_F(XglFragmentTest, TimeoutHandling) {
    const uint8_t part[] = {'a', 'b'};
    xgl_fragment_message_t complete = {};

    ASSERT_EQ(xgl_fragment_process_ext(&manager, 1, 1, 1, 1, 1, 0, 4, part,
                                       sizeof(part), &complete, 1000),
              XGL_ERR_BUSY);

    EXPECT_EQ(xgl_fragment_process_timeouts(&manager, 7000), 1U);
    EXPECT_EQ(xgl_fragment_get_reassembly_count(&manager), 0U);
}

TEST_F(XglFragmentTest, InvalidParameters) {
    const uint8_t part[] = {'a'};
    xgl_fragment_message_t complete = {};

    EXPECT_EQ(xgl_fragment_process_ext(nullptr, 1, 1, 1, 1, 1, 0, 1, part,
                                       sizeof(part), &complete, 0),
              XGL_ERR_INVALID_PARAM);

    EXPECT_EQ(xgl_fragment_process_ext(&manager, 1, 1, 1, 1, 1, 2, 1, part,
                                       sizeof(part), &complete, 0),
              XGL_ERR_INVALID_FRAME);
}
