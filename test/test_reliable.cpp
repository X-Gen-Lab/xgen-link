/**
 * \file            test_reliable.cpp
 * \brief           Reliable transmission unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <xgl/internal/xgl_reliable.h>
#include <xgl/xgl_types.h>
#include <xgl/xgl_error.h>
#include <xgl/internal/xgl_wire.h>

using ::testing::_;
using ::testing::Return;
using ::testing::DoAll;
using ::testing::SetArgPointee;

/*---------------------------------------------------------------------------*/
/* Mock Physical Layer                                                       */
/*---------------------------------------------------------------------------*/

class MockPhyOps {
public:
    MOCK_METHOD(xgl_error_t, tx, (const uint8_t* data, size_t len, void* user_data));
    MOCK_METHOD(xgl_error_t, rx, (uint8_t* buffer, size_t* len, void* user_data));
};

static MockPhyOps* g_mock_phy = nullptr;

static xgl_error_t mock_phy_tx(const uint8_t* data, size_t len, void* user_data) {
    if (g_mock_phy != nullptr) {
        return g_mock_phy->tx(data, len, user_data);
    }
    return XGL_OK;
}

static xgl_error_t mock_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    if (g_mock_phy != nullptr) {
        return g_mock_phy->rx(buffer, len, user_data);
    }
    return XGL_OK;
}

template <typename T>
concept HasReliablePacketIndex = requires(T value) {
    value.index_buckets;
};

template <typename T>
concept HasReliablePacketIndexNode = requires(T value) {
    value.index_next;
};

static_assert(HasReliablePacketIndex<xgl_reliable_queue_t>,
              "reliable queue must index packet_number lookups for multi-node ACK/SACK");
static_assert(HasReliablePacketIndexNode<xgl_reliable_packet_t>,
              "reliable packets must carry an index link for O(1)-bucket lookup");

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglReliableTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_mock_phy = &mock_phy;
        
        phy_ops.tx = mock_phy_tx;
        phy_ops.rx = mock_phy_rx;
        phy_ops.user_data = nullptr;
        
        xgl_reliable_init(&queue, 3, nullptr);
    }
    
    void TearDown() override {
        xgl_reliable_destroy(&queue);
        g_mock_phy = nullptr;
    }
    
    xgl_reliable_queue_t queue;
    xgl_phy_ops_t phy_ops;
    MockPhyOps mock_phy;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, InitializeQueue) {
    xgl_reliable_queue_t q;
    xgl_error_t err = xgl_reliable_init(&q, 5, nullptr);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_TRUE(xgl_reliable_is_empty(&q));
    EXPECT_EQ(xgl_reliable_get_count(&q), 0);
    
    xgl_reliable_destroy(&q);
}

TEST_F(XglReliableTest, InitializeWithNullPointer) {
    xgl_error_t err = xgl_reliable_init(nullptr, 5, nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

/*---------------------------------------------------------------------------*/
/* Add Packet Tests                                                          */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, AddPacketToQueue) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    
    xgl_error_t err = xgl_reliable_add_packet_number(
        &queue,
        data, sizeof(data),
        1,      /* source_id */
        2,      /* target_id */
        10,     /* packet_number */
        5,      /* data_type */
        3,      /* priority */
        1000,   /* timeout_ms */
        &phy_ops
    );
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_FALSE(xgl_reliable_is_empty(&queue));
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

TEST_F(XglReliableTest, AddMultiplePackets) {
    uint8_t data1[] = {0x01, 0x02};
    uint8_t data2[] = {0x03, 0x04};
    uint8_t data3[] = {0x05, 0x06};
    
    xgl_reliable_add_packet_number(&queue, data1, sizeof(data1), 1, 2, 10, 5, 3, 1000, &phy_ops);
    xgl_reliable_add_packet_number(&queue, data2, sizeof(data2), 1, 2, 11, 5, 3, 1000, &phy_ops);
    xgl_reliable_add_packet_number(&queue, data3, sizeof(data3), 1, 2, 12, 5, 3, 1000, &phy_ops);
    
    EXPECT_EQ(xgl_reliable_get_count(&queue), 3);
}

TEST_F(XglReliableTest, AddPacketWithNullData) {
    xgl_error_t err = xgl_reliable_add_packet_number(
        &queue,
        nullptr, 10,
        1, 2, 10, 5, 3, 1000, &phy_ops
    );
    
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglReliableTest, AddPacketWithZeroLength) {
    uint8_t data[] = {0x01};
    
    xgl_error_t err = xgl_reliable_add_packet_number(
        &queue,
        data, 0,
        1, 2, 10, 5, 3, 1000, &phy_ops
    );
    
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglReliableTest, AddPacketWithNullPhyAllowed) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_error_t err = xgl_reliable_add_packet_number(
        &queue,
        data, sizeof(data),
        1, 2, 10, 5, 3, 1000, nullptr
    );
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

/*---------------------------------------------------------------------------*/
/* Remove Packet Tests                                                       */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, RemovePacketBySeqNum) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
    
    xgl_error_t err = xgl_reliable_remove_packet_number(&queue, 10, 2);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 0);
}

TEST_F(XglReliableTest, RemoveNonExistentPacket) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    xgl_error_t err = xgl_reliable_remove_packet_number(&queue, 99, 2);
    EXPECT_EQ(err, XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

TEST_F(XglReliableTest, RemovePacketWithWrongTargetId) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    xgl_error_t err = xgl_reliable_remove_packet_number(&queue, 10, 99);
    EXPECT_EQ(err, XGL_ERR_SEQUENCE_ERROR);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

/*---------------------------------------------------------------------------*/
/* Find Packet Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, FindPacketBySeqNum) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->packet_number, 10);
    EXPECT_EQ(packet->target_id, 2);
    EXPECT_EQ(packet->data_len, sizeof(data));
}

TEST_F(XglReliableTest, FindNonExistentPacket) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 99, 2);
    EXPECT_EQ(packet, nullptr);
}

TEST_F(XglReliableTest, FindAndRemovePacketBy32BitPacketNumber) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    const uint32_t first_packet_number = 0x01020304U;
    const uint32_t second_packet_number = 0x02020304U;

    ASSERT_EQ(xgl_reliable_add_packet_number(&queue,
                                             data,
                                             sizeof(data),
                                             0x1234,
                                             0x2345,
                                             first_packet_number,
                                             5,
                                             3,
                                             1000,
                                             &phy_ops),
              XGL_OK);
    ASSERT_EQ(xgl_reliable_add_packet_number(&queue,
                                             data,
                                             sizeof(data),
                                             0x1234,
                                             0x2345,
                                             second_packet_number,
                                             5,
                                             3,
                                             1000,
                                             &phy_ops),
              XGL_OK);

    xgl_reliable_packet_t* first =
        xgl_reliable_find_packet_number(&queue, first_packet_number, 0x2345);
    xgl_reliable_packet_t* second =
        xgl_reliable_find_packet_number(&queue, second_packet_number, 0x2345);

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->packet_number, first_packet_number);
    EXPECT_EQ(second->packet_number, second_packet_number);

    EXPECT_EQ(xgl_reliable_remove_packet_number(&queue, first_packet_number, 0x2345),
              XGL_OK);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, first_packet_number, 0x2345),
              nullptr);
    EXPECT_NE(xgl_reliable_find_packet_number(&queue, second_packet_number, 0x2345),
              nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 1);
}

TEST_F(XglReliableTest, RemovePacketsByAckRangesKeepsHoles) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    const uint16_t target_id = 0x2345;

    const uint32_t packet_numbers[] = {100, 101, 102, 103, 105, 106};
    for (uint32_t packet_number : packet_numbers) {
        ASSERT_EQ(xgl_reliable_add_packet_number(&queue,
                                                 data,
                                                 sizeof(data),
                                                 0x1234,
                                                 target_id,
                                                 packet_number,
                                                 5,
                                                 3,
                                                 1000,
                                                 &phy_ops),
                  XGL_OK);
    }
    ASSERT_EQ(xgl_reliable_add_packet_number(&queue,
                                             data,
                                             sizeof(data),
                                             0x1234,
                                             0x3456,
                                             106,
                                             5,
                                             3,
                                             1000,
                                             &phy_ops),
              XGL_OK);

    const xgl_wire_ack_range_t ranges[] = {
        {.gap = 0, .length = 2},
        {.gap = 2, .length = 2}
    };

    size_t removed = xgl_reliable_remove_ack_ranges(&queue,
                                                    target_id,
                                                    106,
                                                    ranges,
                                                    2);

    EXPECT_EQ(removed, 4U);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 3U);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 106, target_id), nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 105, target_id), nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 102, target_id), nullptr);
    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 101, target_id), nullptr);
    EXPECT_NE(xgl_reliable_find_packet_number(&queue, 103, target_id), nullptr);
    EXPECT_NE(xgl_reliable_find_packet_number(&queue, 100, target_id), nullptr);
    EXPECT_NE(xgl_reliable_find_packet_number(&queue, 106, 0x3456), nullptr);
}

/*---------------------------------------------------------------------------*/
/* Timeout Processing Tests                                                  */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, ProcessTimeoutWithNoPackets) {
    xgl_reliable_packet_t* exhausted = nullptr;
    uint32_t count = xgl_reliable_process_timeouts(&queue, 1000, &exhausted);
    
    EXPECT_EQ(count, 0);
    EXPECT_EQ(exhausted, nullptr);
}

TEST_F(XglReliableTest, ProcessTimeoutBeforeExpiry) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    /* Find packet and set send timestamp */
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    packet->send_timestamp = 100;
    
    /* Process at time 500 (before 1000ms timeout) */
    xgl_reliable_packet_t* exhausted = nullptr;
    uint32_t count = xgl_reliable_process_timeouts(&queue, 500, &exhausted);
    
    EXPECT_EQ(count, 0);
    EXPECT_EQ(exhausted, nullptr);
    EXPECT_EQ(packet->retry_count, 0);
}

TEST_F(XglReliableTest, ProcessTimeoutAfterExpiry) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    /* Find packet and set send timestamp */
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    packet->send_timestamp = 100;
    
    /* Expect retransmission */
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .Times(1)
        .WillOnce(Return(XGL_OK));
    
    /* Process at time 1200 (after 1000ms timeout) */
    xgl_reliable_packet_t* exhausted = nullptr;
    uint32_t count = xgl_reliable_process_timeouts(&queue, 1200, &exhausted);
    
    EXPECT_EQ(count, 1);
    EXPECT_EQ(exhausted, nullptr);
    EXPECT_EQ(packet->retry_count, 1);
}

TEST_F(XglReliableTest, ProcessTimeoutRetryExhaustion) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    /* Find packet and set it to max retries */
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    packet->send_timestamp = 100;
    packet->retry_count = 3;  /* Max retry count */
    
    /* Process timeout - should remove packet */
    xgl_reliable_packet_t* exhausted = nullptr;
    uint32_t count = xgl_reliable_process_timeouts(&queue, 1200, &exhausted);
    
    EXPECT_EQ(count, 0);
    EXPECT_NE(exhausted, nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 0);
    
    /* Clean up exhausted packet */
    if (exhausted != nullptr) {
        free(exhausted->data);
        free(exhausted);
    }
}

TEST_F(XglReliableTest, ProcessTimeoutMultipleRetries) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    packet->send_timestamp = 100;
    
    /* First retry */
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .Times(1)
        .WillOnce(Return(XGL_OK));
    
    xgl_reliable_packet_t* exhausted = nullptr;
    xgl_reliable_process_timeouts(&queue, 1200, &exhausted);
    EXPECT_EQ(packet->retry_count, 1);
    
    /* Second retry */
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .Times(1)
        .WillOnce(Return(XGL_OK));
    
    xgl_reliable_process_timeouts(&queue, 3300, &exhausted);  /* 2000ms backoff */
    EXPECT_EQ(packet->retry_count, 2);
    
    /* Third retry */
    EXPECT_CALL(mock_phy, tx(_, _, _))
        .Times(1)
        .WillOnce(Return(XGL_OK));
    
    xgl_reliable_process_timeouts(&queue, 7400, &exhausted);  /* 4000ms backoff */
    EXPECT_EQ(packet->retry_count, 3);
    
    /* Fourth attempt should exhaust retries */
    xgl_reliable_process_timeouts(&queue, 15500, &exhausted);  /* 8000ms backoff */
    EXPECT_NE(exhausted, nullptr);
    EXPECT_EQ(xgl_reliable_get_count(&queue), 0);
    
    if (exhausted != nullptr) {
        free(exhausted->data);
        free(exhausted);
    }
}

/*---------------------------------------------------------------------------*/
/* Exponential Backoff Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, ExponentialBackoffCalculation) {
    /* Initial timeout: 1000ms */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 0), 1000);   /* 2^0 = 1 */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 1), 2000);   /* 2^1 = 2 */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 2), 4000);   /* 2^2 = 4 */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 3), 8000);   /* 2^3 = 8 */
    EXPECT_EQ(xgl_reliable_calc_backoff(1000, 4), 16000);  /* 2^4 = 16 */
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
    
    xgl_reliable_add_packet_number(&queue, data1, sizeof(data1), 1, 2, 10, 5, 3, 1000, &phy_ops);
    xgl_reliable_add_packet_number(&queue, data2, sizeof(data2), 1, 2, 11, 5, 3, 1000, &phy_ops);
    
    EXPECT_EQ(xgl_reliable_get_count(&queue), 2);
    
    xgl_reliable_clear(&queue);
    
    EXPECT_TRUE(xgl_reliable_is_empty(&queue));
    EXPECT_EQ(xgl_reliable_get_count(&queue), 0);
}

/*---------------------------------------------------------------------------*/
/* Edge Case Tests                                                           */
/*---------------------------------------------------------------------------*/

TEST_F(XglReliableTest, ProcessTimeoutWithUnsentPacket) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_reliable_add_packet_number(&queue, data, sizeof(data), 1, 2, 10, 5, 3, 1000, &phy_ops);
    
    /* Packet has send_timestamp = 0 (not sent yet) */
    xgl_reliable_packet_t* exhausted = nullptr;
    uint32_t count = xgl_reliable_process_timeouts(&queue, 5000, &exhausted);
    
    /* Should not process unsent packets */
    EXPECT_EQ(count, 0);
    EXPECT_EQ(exhausted, nullptr);
}

TEST_F(XglReliableTest, AddPacketWithMaxPriority) {
    uint8_t data[] = {0x01, 0x02};
    
    xgl_error_t err = xgl_reliable_add_packet_number(
        &queue,
        data, sizeof(data),
        1, 2, 10, 5,
        7,      /* Max priority */
        1000, &phy_ops
    );
    
    EXPECT_EQ(err, XGL_OK);
    
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->priority, 7);
}

TEST_F(XglReliableTest, AddPacketWithLargeData) {
    uint8_t data[1024];
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }
    
    xgl_error_t err = xgl_reliable_add_packet_number(
        &queue,
        data, sizeof(data),
        1, 2, 10, 5, 3, 1000, &phy_ops
    );
    
    EXPECT_EQ(err, XGL_OK);
    
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    EXPECT_EQ(packet->data_len, sizeof(data));
    EXPECT_EQ(memcmp(packet->data, data, sizeof(data)), 0);
}

