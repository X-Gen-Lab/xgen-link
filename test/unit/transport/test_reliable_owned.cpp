/**
 * \file            test_reliable_owned.cpp
 * \brief           Owned reliable record removal and queue isolation
 */

#include "test_host_allocator.h"
#include <transport/xgl_reliable.h>

#include <gtest/gtest.h>

namespace {

class XglReliableOwnedTest : public ::testing::Test {
  protected:
    xgl_reliable_queue_t queue = {};
    xgl_reliable_queue_t other = {};

    void SetUp() override {
        ASSERT_EQ(xgl_reliable_init(&queue, xgl_test_host_allocator()), XGL_OK);
        ASSERT_EQ(xgl_reliable_init(&other, xgl_test_host_allocator()), XGL_OK);
    }

    void TearDown() override {
        xgl_reliable_destroy(&queue);
        xgl_reliable_destroy(&other);
    }

    xgl_reliable_packet_t* Add(xgl_reliable_queue_t& owner, uint32_t number) {
        const uint8_t data[] = {0x31U, 0x42U};
        xgl_reliable_packet_t* packet = nullptr;
        EXPECT_EQ(xgl_reliable_add_packet_number(&owner, data, sizeof(data), 1U,
                                                 2U, number, 3U, 0U, 1000,
                                                 &packet),
                  XGL_OK);
        return packet;
    }
};

}  // namespace

TEST_F(XglReliableOwnedTest, RemovesKnownRecordAndItsExtensions) {
    auto* first = Add(queue, 10U);
    auto* second = Add(queue, 11U);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    const uint8_t extension[] = {0x71U, 0x01U, 0x55U};
    ASSERT_EQ(xgl_reliable_set_packet_extensions(&queue, first, extension,
                                                 sizeof(extension)),
              XGL_OK);
    ASSERT_EQ(xgl_reliable_remove_owned(&queue, first), XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1U);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 10U, 2U), nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 11U, 2U), second);
    ASSERT_EQ(xgl_reliable_remove_owned(&queue, second), XGL_OK);
    EXPECT_TRUE(xgl_reliable_is_empty(&queue));
}

TEST_F(XglReliableOwnedTest, RejectsLiveRecordFromAnotherQueue) {
    auto* owned = Add(queue, 10U);
    auto* foreign = Add(other, 10U);
    ASSERT_NE(owned, nullptr);
    ASSERT_NE(foreign, nullptr);
    EXPECT_EQ(xgl_reliable_remove_owned(&queue, foreign),
              XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1U);
    EXPECT_EQ(xgl_reliable_get_count(&other), 1U);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 10U, 2U), owned);
    EXPECT_EQ(xgl_reliable_find_packet_number(&other, 10U, 2U), foreign);
}

TEST_F(XglReliableOwnedTest, EmptyQueueCannotRemoveAnotherOwnersRecord) {
    auto* foreign = Add(other, 10U);
    ASSERT_NE(foreign, nullptr);
    EXPECT_EQ(xgl_reliable_remove_owned(&queue, foreign),
              XGL_ERR_SEQUENCE_ERROR);
    EXPECT_TRUE(xgl_reliable_is_empty(&queue));
    EXPECT_EQ(xgl_reliable_get_count(&other), 1U);
}

TEST_F(XglReliableOwnedTest, NullRemovalLeavesQueueUnchanged) {
    auto* owned = Add(queue, 10U);
    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(xgl_reliable_remove_owned(nullptr, owned), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_reliable_remove_owned(&queue, nullptr), XGL_ERR_NULL_POINTER);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 10U, 2U), owned);
}

TEST_F(XglReliableOwnedTest,
       ClearCollisionChainReleasesEveryOwnerAndAllowsReuse) {
    constexpr uint32_t count = 64U;
    for (uint32_t index = 0U; index < count; ++index) {
        auto* packet = Add(queue, index * XGL_RELIABLE_INDEX_BUCKETS);
        ASSERT_NE(packet, nullptr);
        const uint8_t extensions[] = {0x71U, 0x01U, 0x55U};
        ASSERT_EQ(xgl_reliable_set_packet_extensions(&queue, packet, extensions,
                                                     sizeof(extensions)),
                  XGL_OK);
    }
    auto* foreign = Add(other, 0U);
    ASSERT_NE(foreign, nullptr);
    xgl_reliable_clear(&queue);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 0U);
    for (uint32_t index = 0U; index < count; ++index) {
        EXPECT_EQ(xgl_reliable_find_packet_number(
                      &queue, index * XGL_RELIABLE_INDEX_BUCKETS, 2U),
                  nullptr);
    }
    EXPECT_EQ(xgl_reliable_find_packet_number(&other, 0U, 2U), foreign);
    auto* reused = Add(queue, 0U);
    ASSERT_NE(reused, nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 0U, 2U), reused);
    xgl_reliable_clear(&queue);
    xgl_reliable_clear(&queue);
    EXPECT_TRUE(xgl_reliable_is_empty(&queue));
}
