/**
 * \file            test_packet_pool.cpp
 * \brief           Packet object pool unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/xgl.h>
#include <xgl/xgl_packet_pool.h>
#include <vector>
#include <cstring>

template <typename T>
concept HasLegacyPacketSequenceFields = requires(T value) {
    value.seq_num;
    value.ack_num;
};

static_assert(!HasLegacyPacketSequenceFields<xgl_packet_t>,
              "xgl_packet_t must not expose legacy seq_num/ack_num fields");

/*---------------------------------------------------------------------------*/
/* Test Fixtures                                                             */
/*---------------------------------------------------------------------------*/

class XglPacketPoolTest : public ::testing::Test {
protected:
    static constexpr size_t POOL_SIZE = 10;
    
    xgl_packet_pool_t pool;
    
    void SetUp() override {
        memset(&pool, 0, sizeof(pool));
    }
    
    void TearDown() override {
        xgl_packet_pool_destroy(&pool);
    }
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglPacketPoolTest, InitSuccess) {
    int result = xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    EXPECT_EQ(result, 0);
    EXPECT_EQ(pool.total_count, POOL_SIZE);
    EXPECT_EQ(pool.free_count, POOL_SIZE);
    EXPECT_EQ(pool.peak_used, 0);
    EXPECT_NE(pool.packets, nullptr);
}

TEST_F(XglPacketPoolTest, InitNullPool) {
    int result = xgl_packet_pool_init(nullptr, POOL_SIZE, nullptr);
    EXPECT_EQ(result, -1);
}

TEST_F(XglPacketPoolTest, InitZeroCount) {
    int result = xgl_packet_pool_init(&pool, 0, nullptr);
    EXPECT_EQ(result, -1);
}

TEST_F(XglPacketPoolTest, Destroy) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    xgl_packet_pool_destroy(&pool);
    
    EXPECT_EQ(pool.packets, nullptr);
    EXPECT_EQ(pool.total_count, 0);
    EXPECT_EQ(pool.free_count, 0);
}

TEST_F(XglPacketPoolTest, DestroyNull) {
    /* Should not crash */
    xgl_packet_pool_destroy(nullptr);
}

/*---------------------------------------------------------------------------*/
/* Packet Allocation Tests                                                   */
/*---------------------------------------------------------------------------*/

TEST_F(XglPacketPoolTest, AllocSinglePacket) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    xgl_packet_t* packet = xgl_packet_alloc(&pool);
    
    EXPECT_NE(packet, nullptr);
    EXPECT_EQ(pool.free_count, POOL_SIZE - 1);
    EXPECT_EQ(pool.peak_used, 1);
}

TEST_F(XglPacketPoolTest, AllocMultiplePackets) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    std::vector<xgl_packet_t*> packets;
    
    for (size_t i = 0; i < POOL_SIZE; i++) {
        xgl_packet_t* packet = xgl_packet_alloc(&pool);
        EXPECT_NE(packet, nullptr);
        packets.push_back(packet);
    }
    
    EXPECT_EQ(packets.size(), POOL_SIZE);
    EXPECT_EQ(pool.free_count, 0);
    EXPECT_EQ(pool.peak_used, POOL_SIZE);
}

TEST_F(XglPacketPoolTest, AllocAllPackets) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    /* Allocate all packets */
    for (size_t i = 0; i < POOL_SIZE; i++) {
        xgl_packet_t* packet = xgl_packet_alloc(&pool);
        EXPECT_NE(packet, nullptr);
    }
    
    /* Pool should be exhausted */
    EXPECT_TRUE(xgl_packet_pool_is_full(&pool));
    
    /* Next allocation should fail */
    xgl_packet_t* packet = xgl_packet_alloc(&pool);
    EXPECT_EQ(packet, nullptr);
}

TEST_F(XglPacketPoolTest, AllocNullPool) {
    xgl_packet_t* packet = xgl_packet_alloc(nullptr);
    EXPECT_EQ(packet, nullptr);
}

TEST_F(XglPacketPoolTest, AllocUniquePackets) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    std::vector<xgl_packet_t*> packets;
    
    /* Allocate all packets */
    for (size_t i = 0; i < POOL_SIZE; i++) {
        xgl_packet_t* packet = xgl_packet_alloc(&pool);
        ASSERT_NE(packet, nullptr);
        packets.push_back(packet);
    }
    
    /* Verify all packets are unique */
    for (size_t i = 0; i < packets.size(); i++) {
        for (size_t j = i + 1; j < packets.size(); j++) {
            EXPECT_NE(packets[i], packets[j]);
        }
    }
}

TEST_F(XglPacketPoolTest, AllocInitializesFields) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    xgl_packet_t* packet = xgl_packet_alloc(&pool);
    ASSERT_NE(packet, nullptr);
    
    /* Verify all fields are initialized to zero */
    EXPECT_EQ(packet->source_id, 0);
    EXPECT_EQ(packet->target_id, 0);
    EXPECT_EQ(packet->connection_id, 0U);
    EXPECT_EQ(packet->packet_number, 0U);
    EXPECT_EQ(packet->session_epoch, 0U);
    EXPECT_EQ(packet->version, 0);
    EXPECT_EQ(packet->data_type, 0);
    EXPECT_EQ(packet->reliable, 0);
    EXPECT_EQ(packet->fragment, 0);
    EXPECT_EQ(packet->encrypt, 0);
    EXPECT_EQ(packet->priority, 0);
    EXPECT_EQ(packet->compress, 0);
    EXPECT_EQ(packet->data, nullptr);
    EXPECT_EQ(packet->retry_count, 0);
    EXPECT_EQ(packet->wait_time_ms, 0);
    EXPECT_EQ(packet->send_timestamp, 0);
    EXPECT_EQ(packet->phy, nullptr);
}

/*---------------------------------------------------------------------------*/
/* Packet Deallocation Tests                                                 */
/*---------------------------------------------------------------------------*/

TEST_F(XglPacketPoolTest, FreeSinglePacket) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    xgl_packet_t* packet = xgl_packet_alloc(&pool);
    ASSERT_NE(packet, nullptr);
    
    size_t free_before = pool.free_count;
    xgl_packet_free(&pool, packet);
    
    EXPECT_EQ(pool.free_count, free_before + 1);
}

TEST_F(XglPacketPoolTest, FreeMultiplePackets) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    std::vector<xgl_packet_t*> packets;
    for (size_t i = 0; i < 5; i++) {
        xgl_packet_t* packet = xgl_packet_alloc(&pool);
        ASSERT_NE(packet, nullptr);
        packets.push_back(packet);
    }
    
    /* Free all packets */
    for (xgl_packet_t* packet : packets) {
        xgl_packet_free(&pool, packet);
    }
    
    EXPECT_EQ(pool.free_count, pool.total_count);
    EXPECT_TRUE(xgl_packet_pool_is_empty(&pool));
}

TEST_F(XglPacketPoolTest, FreeNullPool) {
    xgl_packet_t packet;
    /* Should not crash */
    xgl_packet_free(nullptr, &packet);
}

TEST_F(XglPacketPoolTest, FreeNullPacket) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    /* Should not crash */
    xgl_packet_free(&pool, nullptr);
}

TEST_F(XglPacketPoolTest, AllocFreeRealloc) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    /* Allocate a packet */
    xgl_packet_t* packet1 = xgl_packet_alloc(&pool);
    ASSERT_NE(packet1, nullptr);
    
    /* Free it */
    xgl_packet_free(&pool, packet1);
    
    /* Allocate again - should get a packet (may or may not be the same) */
    xgl_packet_t* packet2 = xgl_packet_alloc(&pool);
    EXPECT_NE(packet2, nullptr);
    
    xgl_packet_free(&pool, packet2);
}

/*---------------------------------------------------------------------------*/
/* Packet Data Reference Counting Tests                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglPacketPoolTest, PacketDataCreate) {
    const uint8_t test_data[] = {1, 2, 3, 4, 5};
    size_t test_len = sizeof(test_data);
    
    xgl_packet_data_t* pkt_data = xgl_packet_data_create(test_data, test_len, nullptr);
    
    ASSERT_NE(pkt_data, nullptr);
    EXPECT_EQ(pkt_data->ref_count, 1);
    EXPECT_EQ(pkt_data->data_len, test_len);
    EXPECT_NE(pkt_data->data, nullptr);
    EXPECT_EQ(memcmp(pkt_data->data, test_data, test_len), 0);
    
    xgl_packet_data_unref(pkt_data, nullptr);
}

TEST_F(XglPacketPoolTest, PacketDataCreateNullData) {
    xgl_packet_data_t* pkt_data = xgl_packet_data_create(nullptr, 10, nullptr);
    EXPECT_EQ(pkt_data, nullptr);
}

TEST_F(XglPacketPoolTest, PacketDataCreateZeroLength) {
    const uint8_t test_data[] = {1, 2, 3};
    xgl_packet_data_t* pkt_data = xgl_packet_data_create(test_data, 0, nullptr);
    EXPECT_EQ(pkt_data, nullptr);
}

TEST_F(XglPacketPoolTest, PacketDataRef) {
    const uint8_t test_data[] = {1, 2, 3, 4, 5};
    xgl_packet_data_t* pkt_data = xgl_packet_data_create(test_data, sizeof(test_data), nullptr);
    ASSERT_NE(pkt_data, nullptr);
    
    EXPECT_EQ(pkt_data->ref_count, 1);
    
    xgl_packet_data_ref(pkt_data);
    EXPECT_EQ(pkt_data->ref_count, 2);
    
    xgl_packet_data_ref(pkt_data);
    EXPECT_EQ(pkt_data->ref_count, 3);
    
    xgl_packet_data_unref(pkt_data, nullptr);
    xgl_packet_data_unref(pkt_data, nullptr);
    xgl_packet_data_unref(pkt_data, nullptr);
}

TEST_F(XglPacketPoolTest, PacketDataRefNull) {
    /* Should not crash */
    xgl_packet_data_ref(nullptr);
}

TEST_F(XglPacketPoolTest, PacketDataUnref) {
    const uint8_t test_data[] = {1, 2, 3, 4, 5};
    xgl_packet_data_t* pkt_data = xgl_packet_data_create(test_data, sizeof(test_data), nullptr);
    ASSERT_NE(pkt_data, nullptr);
    
    xgl_packet_data_ref(pkt_data);
    xgl_packet_data_ref(pkt_data);
    EXPECT_EQ(pkt_data->ref_count, 3);
    
    xgl_packet_data_unref(pkt_data, nullptr);
    EXPECT_EQ(pkt_data->ref_count, 2);
    
    xgl_packet_data_unref(pkt_data, nullptr);
    EXPECT_EQ(pkt_data->ref_count, 1);
    
    /* Last unref should free the data */
    xgl_packet_data_unref(pkt_data, nullptr);
}

TEST_F(XglPacketPoolTest, PacketDataUnrefNull) {
    /* Should not crash */
    xgl_packet_data_unref(nullptr, nullptr);
}

TEST_F(XglPacketPoolTest, PacketFreeWithData) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    const uint8_t test_data[] = {1, 2, 3, 4, 5};
    xgl_packet_data_t* pkt_data = xgl_packet_data_create(test_data, sizeof(test_data), nullptr);
    ASSERT_NE(pkt_data, nullptr);
    
    xgl_packet_t* packet = xgl_packet_alloc(&pool);
    ASSERT_NE(packet, nullptr);
    
    packet->data = pkt_data;
    
    /* Free packet should also decrement data reference count */
    xgl_packet_free(&pool, packet);
}

/*---------------------------------------------------------------------------*/
/* Query Operation Tests                                                     */
/*---------------------------------------------------------------------------*/

TEST_F(XglPacketPoolTest, GetFreeCount) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    size_t initial_free = xgl_packet_pool_get_free_count(&pool);
    EXPECT_EQ(initial_free, POOL_SIZE);
    
    xgl_packet_alloc(&pool);
    EXPECT_EQ(xgl_packet_pool_get_free_count(&pool), initial_free - 1);
}

TEST_F(XglPacketPoolTest, GetUsedCount) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    EXPECT_EQ(xgl_packet_pool_get_used_count(&pool), 0);
    
    xgl_packet_alloc(&pool);
    EXPECT_EQ(xgl_packet_pool_get_used_count(&pool), 1);
    
    xgl_packet_alloc(&pool);
    EXPECT_EQ(xgl_packet_pool_get_used_count(&pool), 2);
}

TEST_F(XglPacketPoolTest, GetPeakUsed) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    EXPECT_EQ(xgl_packet_pool_get_peak_used(&pool), 0);
    
    xgl_packet_t* packet1 = xgl_packet_alloc(&pool);
    EXPECT_EQ(xgl_packet_pool_get_peak_used(&pool), 1);
    
    xgl_packet_t* packet2 = xgl_packet_alloc(&pool);
    EXPECT_EQ(xgl_packet_pool_get_peak_used(&pool), 2);
    
    xgl_packet_t* packet3 = xgl_packet_alloc(&pool);
    EXPECT_EQ(xgl_packet_pool_get_peak_used(&pool), 3);
    
    /* Free one packet - peak should remain */
    xgl_packet_free(&pool, packet2);
    EXPECT_EQ(xgl_packet_pool_get_peak_used(&pool), 3);
    
    /* Free all - peak should still remain */
    xgl_packet_free(&pool, packet1);
    xgl_packet_free(&pool, packet3);
    EXPECT_EQ(xgl_packet_pool_get_peak_used(&pool), 3);
}

TEST_F(XglPacketPoolTest, IsEmpty) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    EXPECT_TRUE(xgl_packet_pool_is_empty(&pool));
    
    xgl_packet_t* packet = xgl_packet_alloc(&pool);
    EXPECT_FALSE(xgl_packet_pool_is_empty(&pool));
    
    xgl_packet_free(&pool, packet);
    EXPECT_TRUE(xgl_packet_pool_is_empty(&pool));
}

TEST_F(XglPacketPoolTest, IsFull) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    EXPECT_FALSE(xgl_packet_pool_is_full(&pool));
    
    /* Allocate all packets */
    std::vector<xgl_packet_t*> packets;
    for (size_t i = 0; i < POOL_SIZE; i++) {
        xgl_packet_t* packet = xgl_packet_alloc(&pool);
        ASSERT_NE(packet, nullptr);
        packets.push_back(packet);
    }
    
    EXPECT_TRUE(xgl_packet_pool_is_full(&pool));
    
    /* Free one packet */
    xgl_packet_free(&pool, packets[0]);
    EXPECT_FALSE(xgl_packet_pool_is_full(&pool));
    
    /* Cleanup */
    for (size_t i = 1; i < packets.size(); i++) {
        xgl_packet_free(&pool, packets[i]);
    }
}

TEST_F(XglPacketPoolTest, ResetStats) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    /* Allocate some packets */
    xgl_packet_t* packet1 = xgl_packet_alloc(&pool);
    xgl_packet_t* packet2 = xgl_packet_alloc(&pool);
    xgl_packet_t* packet3 = xgl_packet_alloc(&pool);
    
    EXPECT_EQ(xgl_packet_pool_get_peak_used(&pool), 3);
    
    /* Free one packet */
    xgl_packet_free(&pool, packet2);
    EXPECT_EQ(xgl_packet_pool_get_used_count(&pool), 2);
    
    /* Reset stats - peak should be set to current usage */
    xgl_packet_pool_reset_stats(&pool);
    EXPECT_EQ(xgl_packet_pool_get_peak_used(&pool), 2);
    
    /* Cleanup */
    xgl_packet_free(&pool, packet1);
    xgl_packet_free(&pool, packet3);
}

/*---------------------------------------------------------------------------*/
/* Stress Tests                                                              */
/*---------------------------------------------------------------------------*/

TEST_F(XglPacketPoolTest, StressAllocFree) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    std::vector<xgl_packet_t*> packets;
    
    /* Perform many alloc/free cycles */
    for (int cycle = 0; cycle < 100; cycle++) {
        /* Allocate all packets */
        packets.clear();
        for (size_t i = 0; i < POOL_SIZE; i++) {
            xgl_packet_t* packet = xgl_packet_alloc(&pool);
            ASSERT_NE(packet, nullptr);
            packets.push_back(packet);
        }
        
        EXPECT_TRUE(xgl_packet_pool_is_full(&pool));
        
        /* Free all packets */
        for (xgl_packet_t* packet : packets) {
            xgl_packet_free(&pool, packet);
        }
        
        EXPECT_TRUE(xgl_packet_pool_is_empty(&pool));
    }
}

TEST_F(XglPacketPoolTest, StressRandomAllocFree) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    std::vector<xgl_packet_t*> allocated_packets;
    
    /* Perform random allocations and deallocations */
    for (int i = 0; i < 1000; i++) {
        if (allocated_packets.empty() || (rand() % 2 == 0 && !xgl_packet_pool_is_full(&pool))) {
            /* Allocate */
            xgl_packet_t* packet = xgl_packet_alloc(&pool);
            if (packet != nullptr) {
                allocated_packets.push_back(packet);
            }
        } else {
            /* Free random packet */
            size_t idx = rand() % allocated_packets.size();
            xgl_packet_free(&pool, allocated_packets[idx]);
            allocated_packets.erase(allocated_packets.begin() + idx);
        }
    }
    
    /* Cleanup */
    for (xgl_packet_t* packet : allocated_packets) {
        xgl_packet_free(&pool, packet);
    }
    
    EXPECT_TRUE(xgl_packet_pool_is_empty(&pool));
}

TEST_F(XglPacketPoolTest, StressWithPacketData) {
    xgl_packet_pool_init(&pool, POOL_SIZE, nullptr);
    
    const uint8_t test_data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    /* Allocate packets with data */
    std::vector<xgl_packet_t*> packets;
    for (size_t i = 0; i < POOL_SIZE; i++) {
        xgl_packet_t* packet = xgl_packet_alloc(&pool);
        ASSERT_NE(packet, nullptr);
        
        packet->data = xgl_packet_data_create(test_data, sizeof(test_data), nullptr);
        ASSERT_NE(packet->data, nullptr);
        
        packets.push_back(packet);
    }
    
    /* Free all packets (should also free packet data) */
    for (xgl_packet_t* packet : packets) {
        xgl_packet_free(&pool, packet);
    }
    
    EXPECT_TRUE(xgl_packet_pool_is_empty(&pool));
}
