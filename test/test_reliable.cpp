#include <xgen/memory/libc_allocator.h>
/**
 * \file            test_reliable.cpp
 * \brief           Reliable transmission unit tests
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_reliable.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl_error.h>
#include <xgl/xgl_types.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

/*---------------------------------------------------------------------------*/
/* Mock Physical Layer                                                       */
/*---------------------------------------------------------------------------*/

template <typename T, typename = void>
struct HasReliablePacketIndex : std::false_type {};

template <typename T>
struct HasReliablePacketIndex<
    T, std::void_t<decltype(std::declval<T>().index_buckets)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasReliablePacketIndexNode : std::false_type {};

template <typename T>
struct HasReliablePacketIndexNode<
    T, std::void_t<decltype(std::declval<T>().index_next)>> : std::true_type {};

static_assert(
    HasReliablePacketIndex<xgl_reliable_queue_t>::value,
    "reliable queue must index packet_number lookups for multi-node ACK/SACK");
static_assert(
    HasReliablePacketIndexNode<xgl_reliable_packet_t>::value,
    "reliable packets must carry an index link for O(1)-bucket lookup");

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglReliableTest : public ::testing::Test {
  protected:
    void SetUp() override {
        xgl_reliable_init(&queue, xgm_allocator_libc());
    }

    void TearDown() override {
        xgl_reliable_destroy(&queue);
    }

    xgl_reliable_queue_t queue;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, InitializeQueue) {
    xgl_reliable_queue_t q;
    xgl_error_t err = xgl_reliable_init(&q, xgm_allocator_libc());

    EXPECT_EQ(err, XGL_OK);
    EXPECT_TRUE(xgl_reliable_is_empty(&q));
    EXPECT_EQ(xgl_reliable_get_count(&q), 0);

    xgl_reliable_destroy(&q);
}

TEST_F(XglReliableTest, InitializeWithNullPointer) {
    xgl_error_t err = xgl_reliable_init(nullptr, xgm_allocator_libc());
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Add Packet Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, AddPacketToQueue) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};

    xgl_error_t err = xgl_reliable_add_packet_number(&queue, data, sizeof(data),
                                                     1,  /* source_id */
                                                     2,  /* target_id */
                                                     10, /* packet_number */
                                                     5,  /* data_type */
                                                     3,  /* priority */
                                                     1000);

    EXPECT_EQ(err, XGL_OK);
    EXPECT_FALSE(xgl_reliable_is_empty(&queue));
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

TEST_F(XglReliableTest, AddMultiplePackets) {
    uint8_t data1[] = {0x01, 0x02};
    uint8_t data2[] = {0x03, 0x04};
    uint8_t data3[] = {0x05, 0x06};

    xgl_reliable_add_packet_number(&queue, data1, sizeof(data1), 1, 2, 10, 5, 3,
                                   1000);
    xgl_reliable_add_packet_number(&queue, data2, sizeof(data2), 1, 2, 11, 5, 3,
                                   1000);
    xgl_reliable_add_packet_number(&queue, data3, sizeof(data3), 1, 2, 12, 5, 3,
                                   1000);

    EXPECT_EQ(xgl_reliable_get_count(&queue), 3);
}

TEST_F(XglReliableTest, AddPacketWithNullData) {
    xgl_error_t err = xgl_reliable_add_packet_number(&queue, nullptr, 10, 1, 2,
                                                     10, 5, 3, 1000);

    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglReliableTest, AddPacketWithZeroLength) {
    uint8_t data[] = {0x01};

    xgl_error_t err =
        xgl_reliable_add_packet_number(&queue, data, 0, 1, 2, 10, 5, 3, 1000);

    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglReliableTest, AddPacketOwnsNoPhysicalInterface) {
    uint8_t data[] = {0x01, 0x02};

    xgl_error_t err = xgl_reliable_add_packet_number(&queue, data, sizeof(data),
                                                     1, 2, 10, 5, 3, 1000);

    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

/*---------------------------------------------------------------------------*/
/* Remove Packet Tests                                                       */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, RemovePacketBySeqNum) {
    uint8_t data[] = {0x01, 0x02, 0x03};

    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3,
                                   1000);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);

    xgl_error_t err = xgl_reliable_remove_packet_number(&queue, 10, 2);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 0);
}

TEST_F(XglReliableTest, RemoveNonExistentPacket) {
    uint8_t data[] = {0x01, 0x02};

    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3,
                                   1000);

    xgl_error_t err = xgl_reliable_remove_packet_number(&queue, 99, 2);
    EXPECT_EQ(err, XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

TEST_F(XglReliableTest, RemovePacketWithWrongTargetId) {
    uint8_t data[] = {0x01, 0x02};

    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3,
                                   1000);

    xgl_error_t err = xgl_reliable_remove_packet_number(&queue, 10, 99);
    EXPECT_EQ(err, XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

/*---------------------------------------------------------------------------*/
/* Find Packet Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, FindPacketBySeqNum) {
    uint8_t data[] = {0x01, 0x02, 0x03};

    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3,
                                   1000);

    xgl_reliable_packet_t* packet =
        xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->packet_number, 10);
    EXPECT_EQ(packet->target_id, 2);
    EXPECT_EQ(packet->data_len, sizeof(data));
}

TEST_F(XglReliableTest, FindNonExistentPacket) {
    uint8_t data[] = {0x01, 0x02};

    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3,
                                   1000);

    xgl_reliable_packet_t* packet =
        xgl_reliable_find_packet_number(&queue, 99, 2);
    EXPECT_EQ(packet, nullptr);
}

TEST_F(XglReliableTest, FindAndRemovePacketBy32BitPacketNumber) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    const uint32_t first_packet_number = 0x01020304U;
    const uint32_t second_packet_number = 0x02020304U;

    ASSERT_EQ(xgl_reliable_add_packet_number(&queue, data, sizeof(data), 0x1234,
                                             0x2345, first_packet_number, 5, 3,
                                             1000),
              XGL_OK);
    ASSERT_EQ(xgl_reliable_add_packet_number(&queue, data, sizeof(data), 0x1234,
                                             0x2345, second_packet_number, 5, 3,
                                             1000),
              XGL_OK);

    xgl_reliable_packet_t* first =
        xgl_reliable_find_packet_number(&queue, first_packet_number, 0x2345);
    xgl_reliable_packet_t* second =
        xgl_reliable_find_packet_number(&queue, second_packet_number, 0x2345);

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->packet_number, first_packet_number);
    EXPECT_EQ(second->packet_number, second_packet_number);

    EXPECT_EQ(
        xgl_reliable_remove_packet_number(&queue, first_packet_number, 0x2345),
        XGL_OK);
    EXPECT_EQ(
        xgl_reliable_find_packet_number(&queue, first_packet_number, 0x2345),
        nullptr);
    EXPECT_NE(
        xgl_reliable_find_packet_number(&queue, second_packet_number, 0x2345),
        nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

/*---------------------------------------------------------------------------*/
/* Timeout Processing Tests                                                  */
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* Exponential Backoff Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, ExponentialBackoffCalculation) {
    /* Initial timeout: 1000ms */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 0), 1000);  /* 2^0 = 1 */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 1), 2000);  /* 2^1 = 2 */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 2), 4000);  /* 2^2 = 4 */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 3), 8000);  /* 2^3 = 8 */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 4), 16000); /* 2^4 = 16 */
}

TEST_F(XglReliableTest, ExponentialBackoffCapping) {
    /* Should cap at 30 seconds */
    int32_t backoff = xgl_reliable_calc_backoff(1000, 10);
    EXPECT_LE(backoff, 30000);
}

TEST_F(XglReliableTest, ExponentialBackoffWithLargeRetryCount) {
    /* Should handle large retry counts without overflow */
    int32_t backoff = xgl_reliable_calc_backoff(1000, 20);
    EXPECT_LE(backoff, 30000);
    EXPECT_GT(backoff, 0);
}

TEST_F(XglReliableTest, ExponentialBackoffSaturatesBeforeSignedOverflow) {
    const uint8_t retry_counts[] = {0U, 1U, 2U, 10U, UINT8_MAX};
    for (uint8_t retries : retry_counts) {
        EXPECT_EQ(xgl_reliable_calc_backoff(INT32_MAX, retries), 30000);
        EXPECT_EQ(xgl_reliable_calc_backoff(INT32_MAX / 2 + 1, retries), 30000);
    }
}

TEST_F(XglReliableTest, ExponentialBackoffHonorsSaturationBoundary) {
    EXPECT_EQ(xgl_reliable_calc_backoff(14999, 1), 29998);
    EXPECT_EQ(xgl_reliable_calc_backoff(15000, 1), 30000);
    EXPECT_EQ(xgl_reliable_calc_backoff(15001, 1), 30000);
    EXPECT_EQ(xgl_reliable_calc_backoff(30000, 0), 30000);
    EXPECT_EQ(xgl_reliable_calc_backoff(30001, 0), 30000);
    EXPECT_EQ(xgl_reliable_calc_backoff(1, 10), 1024);
}

TEST_F(XglReliableTest, ExponentialBackoffRejectsNonPositiveTimeout) {
    EXPECT_EQ(xgl_reliable_calc_backoff(0, 1), 0);
    EXPECT_EQ(xgl_reliable_calc_backoff(-1, 1), 0);
    EXPECT_EQ(xgl_reliable_calc_backoff(INT32_MIN, UINT8_MAX), 0);
}

/*---------------------------------------------------------------------------*/
/* Clear Queue Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, ClearEmptyQueue) {
    xgl_reliable_clear(&queue);
    EXPECT_TRUE(xgl_reliable_is_empty(&queue));
}

TEST_F(XglReliableTest, ClearQueueWithPackets) {
    uint8_t data1[] = {0x01, 0x02};
    uint8_t data2[] = {0x03, 0x04};

    xgl_reliable_add_packet_number(&queue, data1, sizeof(data1), 1, 2, 10, 5, 3,
                                   1000);
    xgl_reliable_add_packet_number(&queue, data2, sizeof(data2), 1, 2, 11, 5, 3,
                                   1000);

    EXPECT_EQ(xgl_reliable_get_count(&queue), 2);

    xgl_reliable_clear(&queue);

    EXPECT_TRUE(xgl_reliable_is_empty(&queue));
    EXPECT_EQ(xgl_reliable_get_count(&queue), 0);
}

/*---------------------------------------------------------------------------*/
/* Edge Case Tests                                                           */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, AddPacketWithMaxPriority) {
    uint8_t data[] = {0x01, 0x02};

    xgl_error_t err = xgl_reliable_add_packet_number(
        &queue, data, sizeof(data), 1, 2, 10, 5, 7, /* Max priority */
        1000);

    EXPECT_EQ(err, XGL_OK);

    xgl_reliable_packet_t* packet =
        xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->priority, 7);
}

TEST_F(XglReliableTest, AddPacketWithLargeData) {
    uint8_t data[1024];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    xgl_error_t err = xgl_reliable_add_packet_number(&queue, data, sizeof(data),
                                                     1, 2, 10, 5, 3, 1000);

    EXPECT_EQ(err, XGL_OK);

    xgl_reliable_packet_t* packet =
        xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->data_len, sizeof(data));
    EXPECT_EQ(memcmp(packet->data, data, sizeof(data)), 0);
}
