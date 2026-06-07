/**
 * \file            test_network_properties.cpp
 * \brief           Network layer property tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <xgl/xgl_network.h>
#include <xgl/xgl_route.h>
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
        std::map<uint16_t, xgl_phy_ops_t*> expected_routes;
        
        /* Add random routes */
        for (size_t i = 0; i < num_routes; ++i) {
            uint16_t target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
            
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
            uint16_t target_id = pair.first;
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
        std::set<uint16_t> added_routes;
        std::vector<xgl_phy_ops_t> phy_ops(num_routes);
        
        for (size_t i = 0; i < num_routes; ++i) {
            uint16_t target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
            
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
        uint16_t missing_target_id;
        int attempts = 0;
        do {
            missing_target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
            attempts++;
        } while (added_routes.find(missing_target_id) != added_routes.end() && attempts < 1024);
        
        /* If we couldn't find a missing ID, skip this iteration */
        if (attempts >= 1024) {
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
        uint16_t local_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        
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
        uint16_t other_id;
        do {
            other_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        } while (other_id == local_id || other_id == XGL_BROADCAST_ID);
        
        EXPECT_FALSE(xgl_network_is_local(&ctx, other_id))
            << "Packet addressed to other_id " << (int)other_id 
            << " should not be recognized as local (local_id=" << (int)local_id 
            << ") at iteration " << iteration;
        
        /* Test 4: Address validation */
        uint16_t source_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        /* Ensure source_id is valid (not 0 or broadcast) */
        while (source_id == 0 || source_id == XGL_BROADCAST_ID) {
            source_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
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

