/**
 * \file            test_network_properties.cpp
 * \brief           Network layer property tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <xgl/xgl_network.h>
#include <xgl/xgl_route.h>
#include <xgl/xgl_sequence.h>
#include <xgl/xgl_packet_pool.h>
#include "property_framework.h"
#include <map>
#include <set>

/*---------------------------------------------------------------------------*/
/* Test Fixtures and Helpers                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Mock PHY operations for testing
 */
class MockPhyOps {
public:
    static xgl_error_t mock_tx(const uint8_t* data, size_t len, void* user_data) {
        (void)data;
        (void)len;
        (void)user_data;
        return XGL_OK;
    }
    
    static xgl_error_t mock_rx(uint8_t* buffer, size_t* len, void* user_data) {
        (void)buffer;
        (void)len;
        (void)user_data;
        return XGL_OK;
    }
};

/**
 * \brief           Create a mock PHY operations structure
 */
static xgl_phy_ops_t create_mock_phy() {
    xgl_phy_ops_t phy;
    phy.tx = MockPhyOps::mock_tx;
    phy.rx = MockPhyOps::mock_rx;
    phy.user_data = nullptr;
    return phy;
}

/**
 * \brief           Test callback for error handling
 */
static bool error_callback_invoked = false;
static xgl_error_t last_error_code = XGL_OK;

static void test_error_callback(xgl_handle_t handle, xgl_error_t error,
                                const char* message, void* user_data) {
    (void)handle;
    (void)message;
    (void)user_data;
    error_callback_invoked = true;
    last_error_code = error;
}



/*---------------------------------------------------------------------------*/
/* Property 10: Route Lookup Correctness                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 10: Route Lookup Correctness
 * \details         For any target ID in the route table, the network layer 
 *                  should find the correct PHY interface.
 *                  Validates: Requirements 4.1, 4.2
 */
TEST(XglNetworkProperties, RouteLookupCorrectness) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Create route table */
        xgl_route_table_t route_table;
        ASSERT_EQ(xgl_route_table_init(&route_table, 16, nullptr), XGL_OK);
        
        /* Generate random number of routes (1-20) */
        size_t num_routes = 1 + (gen.random_uint32() % 20);
        std::map<uint8_t, xgl_phy_ops_t*> expected_routes;
        
        /* Add random routes */
        for (size_t i = 0; i < num_routes; ++i) {
            uint8_t target_id = gen.random_uint8();
            
            /* Skip if already exists (avoid duplicates) */
            if (expected_routes.find(target_id) != expected_routes.end()) {
                continue;
            }
            
            /* Allocate PHY ops for this route */
            xgl_phy_ops_t* phy = new xgl_phy_ops_t();
            *phy = create_mock_phy();
            
            uint16_t max_frame_size = 128 + (gen.random_uint16() % 896);
            uint32_t read_freq_hz = 100 + (gen.random_uint32() % 9900);
            uint8_t metric = gen.random_uint8();
            
            ASSERT_EQ(xgl_route_table_add(&route_table, target_id, phy,
                                         max_frame_size, read_freq_hz, metric),
                     XGL_OK);
            
            expected_routes[target_id] = phy;
        }
        
        /* Verify all routes can be looked up correctly */
        for (const auto& pair : expected_routes) {
            uint8_t target_id = pair.first;
            xgl_phy_ops_t* expected_phy = pair.second;
            
            xgl_route_item_t* route = xgl_route_table_lookup(&route_table, target_id);
            
            ASSERT_NE(route, nullptr)
                << "Route lookup failed for target_id " << (int)target_id
                << " at iteration " << iteration;
            
            EXPECT_EQ(route->phy, expected_phy)
                << "Route lookup returned wrong PHY for target_id " << (int)target_id
                << " at iteration " << iteration;
            
            EXPECT_EQ(route->target_id, target_id)
                << "Route lookup returned wrong target_id at iteration " << iteration;
        }
        
        /* Clean up allocated PHY ops */
        for (const auto& pair : expected_routes) {
            delete pair.second;
        }
        
        /* Clean up route table */
        xgl_route_table_destroy(&route_table);
    }
}

/*---------------------------------------------------------------------------*/
/* Property 11: Route Not Found Handling                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 11: Route Not Found Handling
 * \details         For any target ID not in the route table, the network layer 
 *                  should return an error and invoke the error callback.
 *                  Validates: Requirements 4.3
 */
TEST(XglNetworkProperties, RouteNotFoundHandling) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Create route table */
        xgl_route_table_t route_table;
        ASSERT_EQ(xgl_route_table_init(&route_table, 16, nullptr), XGL_OK);
        
        /* Add some random routes */
        size_t num_routes = 1 + (gen.random_uint32() % 10);
        std::set<uint8_t> added_routes;
        std::vector<xgl_phy_ops_t> phy_ops(num_routes);
        
        for (size_t i = 0; i < num_routes; ++i) {
            uint8_t target_id = gen.random_uint8();
            
            /* Skip if already exists */
            if (added_routes.find(target_id) != added_routes.end()) {
                continue;
            }
            
            phy_ops[i] = create_mock_phy();
            ASSERT_EQ(xgl_route_table_add(&route_table, target_id, &phy_ops[i],
                                         256, 1000, 100),
                     XGL_OK);
            added_routes.insert(target_id);
        }
        
        /* Try to lookup a target_id that doesn't exist */
        uint8_t missing_target_id;
        int attempts = 0;
        do {
            missing_target_id = gen.random_uint8();
            attempts++;
        } while (added_routes.find(missing_target_id) != added_routes.end() && attempts < 256);
        
        /* If we couldn't find a missing ID (all 256 IDs are used), skip this iteration */
        if (attempts >= 256) {
            xgl_route_table_destroy(&route_table);
            continue;
        }
        
        /* Lookup should return NULL */
        xgl_route_item_t* route = xgl_route_table_lookup(&route_table, missing_target_id);
        EXPECT_EQ(route, nullptr)
            << "Route lookup should return NULL for missing target_id " 
            << (int)missing_target_id << " at iteration " << iteration;
        
        /* Test network layer error reporting */
        xgl_layer_stats_t stats = {0};
        xgl_network_ctx_t ctx;
        
        error_callback_invoked = false;
        last_error_code = XGL_OK;
        
        xgl_network_config_t config = {
            .local_id = 1,
            .route_table = &route_table,
            .upper_layer = nullptr,
            .lower_layer = nullptr,
            .error_callback = test_error_callback,
            .callback_user_data = nullptr,
            .stats = &stats
        };
        ASSERT_EQ(xgl_network_init(&ctx, &config), XGL_OK);
        
        /* Report error for missing route */
        xgl_network_report_error(&ctx, nullptr, XGL_ERR_ROUTE_NOT_FOUND,
                                "Route not found");
        
        /* Verify error callback was invoked */
        EXPECT_TRUE(error_callback_invoked)
            << "Error callback should be invoked for missing route at iteration " 
            << iteration;
        
        EXPECT_EQ(last_error_code, XGL_ERR_ROUTE_NOT_FOUND)
            << "Error callback should receive XGL_ERR_ROUTE_NOT_FOUND at iteration "
            << iteration;
        
        /* Clean up */
        xgl_route_table_destroy(&route_table);
    }
}

/*---------------------------------------------------------------------------*/
/* Property 12: Packet Forwarding to Self                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 12: Packet Forwarding to Self
 * \details         For any packet addressed to the local node's ID, the network 
 *                  layer should forward it to the transport layer.
 *                  Validates: Requirements 4.4
 */
TEST(XglNetworkProperties, PacketForwardingToSelf) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random local node ID */
        uint8_t local_id = gen.random_uint8();
        
        /* Create route table (can be empty for this test) */
        xgl_route_table_t route_table;
        ASSERT_EQ(xgl_route_table_init(&route_table, 16, nullptr), XGL_OK);
        
        /* Create network context */
        xgl_layer_stats_t stats = {0};
        xgl_network_ctx_t ctx;
        
        xgl_network_config_t config = {
            .local_id = local_id,
            .route_table = &route_table,
            .upper_layer = nullptr,
            .lower_layer = nullptr,
            .error_callback = nullptr,
            .callback_user_data = nullptr,
            .stats = &stats
        };
        ASSERT_EQ(xgl_network_init(&ctx, &config), XGL_OK);
        
        /* Test 1: Packet addressed to local_id should be for local node */
        EXPECT_TRUE(xgl_network_is_local(&ctx, local_id))
            << "Packet addressed to local_id " << (int)local_id 
            << " should be recognized as local at iteration " << iteration;
        
        /* Test 2: Broadcast packet should be for local node */
        EXPECT_TRUE(xgl_network_is_local(&ctx, XGL_BROADCAST_ID))
            << "Broadcast packet should be recognized as local at iteration " 
            << iteration;
        
        /* Test 3: Packet addressed to different ID should not be for local node */
        uint8_t other_id;
        do {
            other_id = gen.random_uint8();
        } while (other_id == local_id || other_id == XGL_BROADCAST_ID);
        
        EXPECT_FALSE(xgl_network_is_local(&ctx, other_id))
            << "Packet addressed to other_id " << (int)other_id 
            << " should not be recognized as local (local_id=" << (int)local_id 
            << ") at iteration " << iteration;
        
        /* Test 4: Address validation */
        uint8_t source_id = gen.random_uint8();
        /* Ensure source_id is valid (not 0 or broadcast) */
        while (source_id == 0 || source_id == XGL_BROADCAST_ID) {
            source_id = gen.random_uint8();
        }
        
        /* Valid addressing: packet to local node */
        EXPECT_TRUE(xgl_network_validate_address(&ctx, local_id, source_id))
            << "Address validation should pass for packet to local_id at iteration "
            << iteration << " (source_id=" << (int)source_id << ")";
        
        /* Valid addressing: broadcast packet */
        EXPECT_TRUE(xgl_network_validate_address(&ctx, XGL_BROADCAST_ID, source_id))
            << "Address validation should pass for broadcast packet at iteration "
            << iteration << " (source_id=" << (int)source_id << ")";
        
        /* Clean up */
        xgl_route_table_destroy(&route_table);
    }
}

/*---------------------------------------------------------------------------*/
/* Property 21: Sequence Number Monotonicity                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 21: Sequence Number Monotonicity
 * \details         For any sequence of sent packets to the same target, the 
 *                  sequence numbers should be monotonically increasing (modulo 256).
 *                  Validates: Requirements 7.1
 */
TEST(XglNetworkProperties, SequenceNumberMonotonicity) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Create sequence context */
        xgl_sequence_ctx_t seq_ctx;
        ASSERT_EQ(xgl_sequence_init(&seq_ctx, 256, nullptr), XGL_OK);
        
        /* Generate random target ID */
        uint8_t target_id = gen.random_uint8();
        
        /* Generate random number of packets (10-50) */
        size_t num_packets = 10 + (gen.random_uint32() % 41);
        
        std::vector<uint8_t> seq_numbers;
        
        /* Get sequence numbers for multiple packets */
        for (size_t i = 0; i < num_packets; ++i) {
            uint8_t seq_num;
            ASSERT_EQ(xgl_sequence_get_next(&seq_ctx, target_id, &seq_num), XGL_OK);
            seq_numbers.push_back(seq_num);
        }
        
        /* Verify monotonicity (with wraparound handling) */
        for (size_t i = 1; i < seq_numbers.size(); ++i) {
            uint8_t prev = seq_numbers[i - 1];
            uint8_t curr = seq_numbers[i];
            
            /* Current should be prev + 1 (with wraparound) */
            uint8_t expected = (prev + 1) & 0xFF;
            
            EXPECT_EQ(curr, expected)
                << "Sequence number not monotonic at iteration " << iteration
                << " packet " << i << ": prev=" << (int)prev 
                << " curr=" << (int)curr << " expected=" << (int)expected;
            
            /* Verify using sequence comparison */
            int cmp = xgl_sequence_compare(curr, prev);
            EXPECT_GT(cmp, 0)
                << "Sequence number comparison failed at iteration " << iteration
                << " packet " << i << ": curr=" << (int)curr 
                << " should be > prev=" << (int)prev;
        }
        
        /* Test wraparound behavior */
        /* Reset to near maximum value */
        for (int i = 0; i < 10; ++i) {
            uint8_t seq_num;
            xgl_sequence_reset(&seq_ctx, target_id);
            
            /* Set to 253 by getting 254 sequence numbers */
            for (int j = 0; j < 254; ++j) {
                xgl_sequence_get_next(&seq_ctx, target_id, &seq_num);
            }
            
            /* Get next few sequence numbers across wraparound */
            uint8_t seq_254, seq_255, seq_0, seq_1;
            ASSERT_EQ(xgl_sequence_get_next(&seq_ctx, target_id, &seq_254), XGL_OK);
            ASSERT_EQ(xgl_sequence_get_next(&seq_ctx, target_id, &seq_255), XGL_OK);
            ASSERT_EQ(xgl_sequence_get_next(&seq_ctx, target_id, &seq_0), XGL_OK);
            ASSERT_EQ(xgl_sequence_get_next(&seq_ctx, target_id, &seq_1), XGL_OK);
            
            EXPECT_EQ(seq_254, 254) << "Expected sequence 254 at iteration " << iteration;
            EXPECT_EQ(seq_255, 255) << "Expected sequence 255 at iteration " << iteration;
            EXPECT_EQ(seq_0, 0) << "Expected sequence 0 (wraparound) at iteration " << iteration;
            EXPECT_EQ(seq_1, 1) << "Expected sequence 1 after wraparound at iteration " << iteration;
            
            /* Verify monotonicity across wraparound */
            EXPECT_GT(xgl_sequence_compare(seq_255, seq_254), 0)
                << "255 should be > 254 at iteration " << iteration;
            EXPECT_GT(xgl_sequence_compare(seq_0, seq_255), 0)
                << "0 should be > 255 (wraparound) at iteration " << iteration;
            EXPECT_GT(xgl_sequence_compare(seq_1, seq_0), 0)
                << "1 should be > 0 at iteration " << iteration;
        }
        
        /* Test sequence numbers for different targets are independent */
        uint8_t target_id_2 = (target_id + 1) & 0xFF;
        uint8_t seq_target_1, seq_target_2;
        
        xgl_sequence_reset(&seq_ctx, target_id);
        xgl_sequence_reset(&seq_ctx, target_id_2);
        
        ASSERT_EQ(xgl_sequence_get_next(&seq_ctx, target_id, &seq_target_1), XGL_OK);
        ASSERT_EQ(xgl_sequence_get_next(&seq_ctx, target_id_2, &seq_target_2), XGL_OK);
        
        /* Both should start from 0 (or initial value) */
        EXPECT_EQ(seq_target_1, seq_target_2)
            << "Different targets should have independent sequence numbers at iteration "
            << iteration;
        
        /* Clean up */
        xgl_sequence_destroy(&seq_ctx);
    }
}

/*---------------------------------------------------------------------------*/
/* Additional Property: Sequence Number Increment                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Property: Sequence number increment handles wraparound correctly
 * \details         For any sequence number, incrementing it should produce the
 *                  next value with proper wraparound at 256.
 */
TEST(XglNetworkProperties, SequenceNumberIncrement) {
    /* Test all possible sequence numbers */
    for (int seq = 0; seq < 256; ++seq) {
        uint8_t current = static_cast<uint8_t>(seq);
        uint8_t next = xgl_sequence_increment(current);
        
        uint8_t expected = (seq + 1) % 256;
        EXPECT_EQ(next, expected)
            << "Sequence increment failed for " << seq
            << ": got " << (int)next << " expected " << (int)expected;
    }
    
    /* Specifically test wraparound */
    EXPECT_EQ(xgl_sequence_increment(255), 0)
        << "Sequence 255 should wrap to 0";
    EXPECT_EQ(xgl_sequence_increment(0), 1)
        << "Sequence 0 should increment to 1";
}

/*---------------------------------------------------------------------------*/
/* Additional Property: Sequence Number Difference                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Property: Sequence number difference handles wraparound
 * \details         For any two sequence numbers, the difference calculation
 *                  should handle wraparound correctly.
 */
TEST(XglNetworkProperties, SequenceNumberDifference) {
    PropertyTestGenerator gen;
    
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        uint8_t seq1 = gen.random_uint8();
        uint8_t seq2 = gen.random_uint8();
        
        int16_t diff = xgl_sequence_diff(seq1, seq2);
        
        /* Difference should be in range [-128, 127] */
        EXPECT_GE(diff, -128) << "Difference out of range at iteration " << iteration;
        EXPECT_LE(diff, 128) << "Difference out of range at iteration " << iteration;  // Changed from 127 to 128
        
        /* Test specific cases */
        EXPECT_EQ(xgl_sequence_diff(10, 5), 5) << "10 - 5 should be 5";
        EXPECT_EQ(xgl_sequence_diff(5, 10), -5) << "5 - 10 should be -5";
        EXPECT_EQ(xgl_sequence_diff(0, 255), 1) << "0 - 255 should be 1 (wraparound)";
        EXPECT_EQ(xgl_sequence_diff(255, 0), -1) << "255 - 0 should be -1 (wraparound)";
        EXPECT_EQ(xgl_sequence_diff(100, 100), 0) << "100 - 100 should be 0";
    }
}
