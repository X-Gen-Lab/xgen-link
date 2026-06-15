/**
 * \file            test_route.cpp
 * \brief           Route table unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/xgl_error.h>
#include <cstring>

/*---------------------------------------------------------------------------*/
/* Test PHY Operations                                                       */
/*---------------------------------------------------------------------------*/

static xgl_error_t test_phy_tx(const uint8_t* data, size_t len, void* user_data) {
    (void)data;
    (void)len;
    (void)user_data;
    return XGL_OK;
}

static xgl_error_t test_phy_rx(uint8_t* buffer, size_t* len, void* user_data) {
    (void)buffer;
    (void)user_data;
    *len = 0;
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglRouteTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Initialize PHY operations */
        phy_ops.tx = test_phy_tx;
        phy_ops.rx = test_phy_rx;
        phy_ops.user_data = nullptr;
        
        /* Initialize route table */
        xgl_error_t err = xgl_route_table_init(&route_table, 4, nullptr);
        ASSERT_EQ(err, XGL_OK);
    }
    
    void TearDown() override {
        xgl_route_table_destroy(&route_table);
    }
    
    static constexpr uint8_t TARGET_ID_1 = 1;
    static constexpr uint8_t TARGET_ID_2 = 2;
    static constexpr uint8_t TARGET_ID_3 = 3;
    static constexpr uint16_t MAX_FRAME_SIZE = 256;
    static constexpr uint32_t READ_FREQ_HZ = 100;
    static constexpr uint8_t METRIC_DEFAULT = 1;
    
    xgl_route_table_t route_table;
    xgl_phy_ops_t phy_ops;
};

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, InitializeRouteTable) {
    xgl_route_table_t table;
    
    xgl_error_t err = xgl_route_table_init(&table, 8, nullptr);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(table.route_count, 0);
    EXPECT_EQ(table.route_capacity, 8);
    EXPECT_NE(table.routes, nullptr);
    
    xgl_route_table_destroy(&table);
}

TEST_F(XglRouteTest, InitializeWithNullPointer) {
    xgl_error_t err = xgl_route_table_init(nullptr, 8, nullptr);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglRouteTest, InitializeWithZeroCapacity) {
    xgl_route_table_t table;
    
    xgl_error_t err = xgl_route_table_init(&table, 0, nullptr);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(table.route_count, 0);
    EXPECT_EQ(table.route_capacity, 0);
    
    xgl_route_table_destroy(&table);
}

/*---------------------------------------------------------------------------*/
/* Add Route Tests                                                           */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, AddSingleRoute) {
    xgl_error_t err = xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops,
                                         MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(route_table.route_count, 1);
}

TEST_F(XglRouteTest, AddMultipleRoutes) {
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops, 
                       MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
    xgl_route_table_add(&route_table, TARGET_ID_2, &phy_ops,
                       MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
    xgl_route_table_add(&route_table, TARGET_ID_3, &phy_ops,
                       MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
    
    EXPECT_EQ(route_table.route_count, 3);
}

TEST_F(XglRouteTest, AddRouteWithNullTable) {
    xgl_error_t err = xgl_route_table_add(nullptr, TARGET_ID_1, &phy_ops,
                                         MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglRouteTest, AddRouteWithNullPhy) {
    xgl_error_t err = xgl_route_table_add(&route_table, TARGET_ID_1, nullptr,
                                         MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
    EXPECT_EQ(err, XGL_ERR_INVALID_PARAM);
}

TEST_F(XglRouteTest, AddDuplicateRouteUpdates) {
    /* Add initial route */
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops,
                       256, 100, 1);
    
    /* Add same target with different parameters */
    xgl_error_t err = xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops,
                                         512, 200, 2);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(route_table.route_count, 1);  // Should not increase
    
    /* Verify updated values */
    xgl_route_item_t* route = xgl_route_table_lookup(&route_table, TARGET_ID_1);
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->max_frame_size, 512);
    EXPECT_EQ(route->read_freq_hz, 200);
    EXPECT_EQ(route->metric, 2);
}

TEST_F(XglRouteTest, AddRouteTriggersDynamicGrowth) {
    /* Add more routes than initial capacity */
    for (uint8_t i = 0; i < 10; i++) {
        xgl_error_t err = xgl_route_table_add(&route_table, i, &phy_ops,
                                             MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
        EXPECT_EQ(err, XGL_OK);
    }
    
    EXPECT_EQ(route_table.route_count, 10);
    EXPECT_GE(route_table.route_capacity, 10);
}

/*---------------------------------------------------------------------------*/
/* Lookup Route Tests                                                        */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, LookupExistingRoute) {
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops,
                       MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
    
    xgl_route_item_t* route = xgl_route_table_lookup(&route_table, TARGET_ID_1);
    
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->target_id, TARGET_ID_1);
    EXPECT_EQ(route->phy, &phy_ops);
    EXPECT_EQ(route->max_frame_size, MAX_FRAME_SIZE);
    EXPECT_EQ(route->read_freq_hz, READ_FREQ_HZ);
    EXPECT_EQ(route->metric, METRIC_DEFAULT);
}

TEST_F(XglRouteTest, LookupNonexistentRoute) {
    xgl_route_item_t* route = xgl_route_table_lookup(&route_table, 99);
    EXPECT_EQ(route, nullptr);
}

TEST_F(XglRouteTest, LookupWithNullTable) {
    xgl_route_item_t* route = xgl_route_table_lookup(nullptr, TARGET_ID_1);
    EXPECT_EQ(route, nullptr);
}

TEST_F(XglRouteTest, LookupAfterMultipleAdds) {
    /* Add multiple routes */
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops, 256, 100, 1);
    xgl_route_table_add(&route_table, TARGET_ID_2, &phy_ops, 512, 200, 2);
    xgl_route_table_add(&route_table, TARGET_ID_3, &phy_ops, 1024, 300, 3);
    
    /* Lookup each route */
    xgl_route_item_t* route1 = xgl_route_table_lookup(&route_table, TARGET_ID_1);
    xgl_route_item_t* route2 = xgl_route_table_lookup(&route_table, TARGET_ID_2);
    xgl_route_item_t* route3 = xgl_route_table_lookup(&route_table, TARGET_ID_3);
    
    ASSERT_NE(route1, nullptr);
    ASSERT_NE(route2, nullptr);
    ASSERT_NE(route3, nullptr);
    
    EXPECT_EQ(route1->max_frame_size, 256);
    EXPECT_EQ(route2->max_frame_size, 512);
    EXPECT_EQ(route3->max_frame_size, 1024);
}

/*---------------------------------------------------------------------------*/
/* Remove Route Tests                                                        */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, RemoveExistingRoute) {
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops,
                       MAX_FRAME_SIZE, READ_FREQ_HZ, METRIC_DEFAULT);
    
    xgl_error_t err = xgl_route_table_remove(&route_table, TARGET_ID_1);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(route_table.route_count, 0);
    
    /* Verify route is gone */
    xgl_route_item_t* route = xgl_route_table_lookup(&route_table, TARGET_ID_1);
    EXPECT_EQ(route, nullptr);
}

TEST_F(XglRouteTest, RemoveNonexistentRoute) {
    xgl_error_t err = xgl_route_table_remove(&route_table, 99);
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
}

TEST_F(XglRouteTest, RemoveWithNullTable) {
    xgl_error_t err = xgl_route_table_remove(nullptr, TARGET_ID_1);
    EXPECT_EQ(err, XGL_ERR_NULL_POINTER);
}

TEST_F(XglRouteTest, RemoveMiddleRoute) {
    /* Add three routes */
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops, 256, 100, 1);
    xgl_route_table_add(&route_table, TARGET_ID_2, &phy_ops, 512, 200, 2);
    xgl_route_table_add(&route_table, TARGET_ID_3, &phy_ops, 1024, 300, 3);
    
    /* Remove middle route */
    xgl_error_t err = xgl_route_table_remove(&route_table, TARGET_ID_2);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(route_table.route_count, 2);
    
    /* Verify remaining routes are still accessible */
    EXPECT_NE(xgl_route_table_lookup(&route_table, TARGET_ID_1), nullptr);
    EXPECT_EQ(xgl_route_table_lookup(&route_table, TARGET_ID_2), nullptr);
    EXPECT_NE(xgl_route_table_lookup(&route_table, TARGET_ID_3), nullptr);
}

/*---------------------------------------------------------------------------*/
/* Update Metric Tests                                                       */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, UpdateMetric) {
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops,
                       MAX_FRAME_SIZE, READ_FREQ_HZ, 1);
    
    xgl_error_t err = xgl_route_table_update_metric(&route_table, TARGET_ID_1, 5);
    
    EXPECT_EQ(err, XGL_OK);
    
    xgl_route_item_t* route = xgl_route_table_lookup(&route_table, TARGET_ID_1);
    ASSERT_NE(route, nullptr);
    EXPECT_EQ(route->metric, 5);
}

TEST_F(XglRouteTest, UpdateMetricNonexistentRoute) {
    xgl_error_t err = xgl_route_table_update_metric(&route_table, 99, 5);
    EXPECT_EQ(err, XGL_ERR_ROUTE_NOT_FOUND);
}

/*---------------------------------------------------------------------------*/
/* Clear Tests                                                               */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, ClearRouteTable) {
    /* Add multiple routes */
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops, 256, 100, 1);
    xgl_route_table_add(&route_table, TARGET_ID_2, &phy_ops, 512, 200, 2);
    xgl_route_table_add(&route_table, TARGET_ID_3, &phy_ops, 1024, 300, 3);
    
    xgl_route_table_clear(&route_table);
    
    EXPECT_EQ(route_table.route_count, 0);
    EXPECT_EQ(xgl_route_table_lookup(&route_table, TARGET_ID_1), nullptr);
    EXPECT_EQ(xgl_route_table_lookup(&route_table, TARGET_ID_2), nullptr);
    EXPECT_EQ(xgl_route_table_lookup(&route_table, TARGET_ID_3), nullptr);
}

/*---------------------------------------------------------------------------*/
/* Load Routes Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, LoadRoutes) {
    xgl_route_item_t routes[] = {
        {TARGET_ID_1, &phy_ops, 256, 100, 1},
        {TARGET_ID_2, &phy_ops, 512, 200, 2},
        {TARGET_ID_3, &phy_ops, 1024, 300, 3}
    };
    
    xgl_error_t err = xgl_route_table_load(&route_table, routes, 3);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(route_table.route_count, 3);
    
    /* Verify all routes are loaded */
    EXPECT_NE(xgl_route_table_lookup(&route_table, TARGET_ID_1), nullptr);
    EXPECT_NE(xgl_route_table_lookup(&route_table, TARGET_ID_2), nullptr);
    EXPECT_NE(xgl_route_table_lookup(&route_table, TARGET_ID_3), nullptr);
}

TEST_F(XglRouteTest, LoadRoutesReplacesExisting) {
    /* Add initial routes */
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops, 128, 50, 1);
    
    /* Load new routes */
    xgl_route_item_t routes[] = {
        {TARGET_ID_2, &phy_ops, 512, 200, 2},
        {TARGET_ID_3, &phy_ops, 1024, 300, 3}
    };
    
    xgl_error_t err = xgl_route_table_load(&route_table, routes, 2);
    
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(route_table.route_count, 2);
    
    /* Old route should be gone */
    EXPECT_EQ(xgl_route_table_lookup(&route_table, TARGET_ID_1), nullptr);
    /* New routes should exist */
    EXPECT_NE(xgl_route_table_lookup(&route_table, TARGET_ID_2), nullptr);
    EXPECT_NE(xgl_route_table_lookup(&route_table, TARGET_ID_3), nullptr);
}

/*---------------------------------------------------------------------------*/
/* Utility Function Tests                                                    */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, GetRouteCount) {
    EXPECT_EQ(xgl_route_table_count(&route_table), 0);
    
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops, 256, 100, 1);
    EXPECT_EQ(xgl_route_table_count(&route_table), 1);
    
    xgl_route_table_add(&route_table, TARGET_ID_2, &phy_ops, 512, 200, 2);
    EXPECT_EQ(xgl_route_table_count(&route_table), 2);
}

TEST_F(XglRouteTest, IsEmpty) {
    EXPECT_TRUE(xgl_route_table_is_empty(&route_table));
    
    xgl_route_table_add(&route_table, TARGET_ID_1, &phy_ops, 256, 100, 1);
    EXPECT_FALSE(xgl_route_table_is_empty(&route_table));
    
    xgl_route_table_clear(&route_table);
    EXPECT_TRUE(xgl_route_table_is_empty(&route_table));
}

/*---------------------------------------------------------------------------*/
/* Performance Tests                                                         */
/*---------------------------------------------------------------------------*/

TEST_F(XglRouteTest, LookupPerformanceWithManyRoutes) {
    /* Add many routes */
    for (uint8_t i = 0; i < 100; i++) {
        xgl_route_table_add(&route_table, i, &phy_ops, 256, 100, 1);
    }
    
    /* Lookup should still be fast (O(1) with hash table) */
    for (int i = 0; i < 1000; i++) {
        xgl_route_item_t* route = xgl_route_table_lookup(&route_table, 50);
        EXPECT_NE(route, nullptr);
    }
}
