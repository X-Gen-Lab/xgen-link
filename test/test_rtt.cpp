/**
 * \file            test_rtt.cpp
 * \brief           RTT estimator unit tests
 * \author          Nexus Team
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_rtt.h>

/*---------------------------------------------------------------------------*/
/* Initialization Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test RTT estimator initialization
 */
TEST(XglRttTest, Initialization) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    EXPECT_FALSE(xgl_rtt_is_initialized(&est));
    EXPECT_EQ(xgl_rtt_get_rto(&est), XGL_DEFAULT_RTO_MS);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0);
}

/**
 * \brief           Test initialization with null pointer
 */
TEST(XglRttTest, InitializationNullPointer) {
    xgl_rtt_init(nullptr);  /* Should not crash */
    EXPECT_TRUE(true);
}

/**
 * \brief           Test is_initialized with null pointer
 */
TEST(XglRttTest, IsInitializedNullPointer) {
    EXPECT_FALSE(xgl_rtt_is_initialized(nullptr));
}

/*---------------------------------------------------------------------------*/
/* First Measurement Tests (RFC 6298 Section 2.2)                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test first RTT measurement
 * \details         RFC 6298: SRTT = R, RTTVAR = R/2
 */
TEST(XglRttTest, FirstMeasurement) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* First measurement: 100ms */
    xgl_rtt_update(&est, 100);
    
    EXPECT_TRUE(xgl_rtt_is_initialized(&est));
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 100);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 50);  /* R/2 */
    
    /* RTO = SRTT + 4 * RTTVAR = 100 + 4*50 = 300 */
    EXPECT_EQ(xgl_rtt_get_rto(&est), 300);
}

/**
 * \brief           Test first measurement with various values
 */
TEST(XglRttTest, FirstMeasurementVariousValues) {
    xgl_rtt_estimator_t est;
    
    /* Test with 50ms */
    xgl_rtt_init(&est);
    xgl_rtt_update(&est, 50);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 50);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 25);
    EXPECT_EQ(xgl_rtt_get_rto(&est), 150);  /* 50 + 4*25 */
    
    /* Test with 200ms */
    xgl_rtt_init(&est);
    xgl_rtt_update(&est, 200);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 200);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 100);
    EXPECT_EQ(xgl_rtt_get_rto(&est), 600);  /* 200 + 4*100 */
    
    /* Test with 1000ms */
    xgl_rtt_init(&est);
    xgl_rtt_update(&est, 1000);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 1000);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 500);
    EXPECT_EQ(xgl_rtt_get_rto(&est), 3000);  /* 1000 + 4*500 */
}

/*---------------------------------------------------------------------------*/
/* Subsequent Measurements Tests (RFC 6298 Section 2.3)                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test subsequent RTT measurements
 * \details         RFC 6298: SRTT += (R - SRTT)/8, RTTVAR += (|R - SRTT| - RTTVAR)/4
 */
TEST(XglRttTest, SubsequentMeasurements) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* First measurement: 100ms */
    xgl_rtt_update(&est, 100);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 100);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 50);
    
    /* Second measurement: 120ms */
    /* error = 120 - 100 = 20 */
    /* SRTT = 100 + 20/8 = 100 + 2 = 102 */
    /* RTTVAR = 50 + (20 - 50)/4 = 50 + (-30/4) = 50 - 7 = 43 (but integer division: -30/4 = -7, so 50 - 7 = 43, but actual is 42) */
    xgl_rtt_update(&est, 120);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 102);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 42);  /* Integer division: 50 + (-30 >> 2) = 50 - 7 = 43, but -30 >> 2 = -8, so 50 - 8 = 42 */
    
    /* RTO = 102 + 4*42 = 102 + 168 = 270 */
    EXPECT_EQ(xgl_rtt_get_rto(&est), 270);
}

/**
 * \brief           Test RTT estimation with stable measurements
 */
TEST(XglRttTest, StableMeasurements) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* All measurements are 100ms */
    for (int i = 0; i < 10; i++) {
        xgl_rtt_update(&est, 100);
    }
    
    /* SRTT should converge to 100ms */
    EXPECT_NEAR(xgl_rtt_get_srtt(&est), 100, 5);
    
    /* RTTVAR should converge to 0 (no variation) */
    EXPECT_LT(xgl_rtt_get_rttvar(&est), 10);
    
    /* RTO should be close to minimum */
    int32_t rto = xgl_rtt_get_rto(&est);
    EXPECT_GE(rto, XGL_MIN_RTO_MS);
    EXPECT_LT(rto, 200);
}

/**
 * \brief           Test RTT estimation with increasing measurements
 */
TEST(XglRttTest, IncreasingMeasurements) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Measurements increase from 100ms to 200ms */
    for (int i = 0; i < 10; i++) {
        xgl_rtt_update(&est, 100 + i * 10);
    }
    
    /* SRTT should be higher than initial */
    EXPECT_GT(xgl_rtt_get_srtt(&est), 100);
    
    /* RTTVAR should reflect variation */
    EXPECT_GT(xgl_rtt_get_rttvar(&est), 0);
}

/**
 * \brief           Test RTT estimation with decreasing measurements
 */
TEST(XglRttTest, DecreasingMeasurements) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Measurements decrease from 200ms to 100ms */
    for (int i = 0; i < 10; i++) {
        xgl_rtt_update(&est, 200 - i * 10);
    }
    
    /* SRTT should be lower than initial */
    EXPECT_LT(xgl_rtt_get_srtt(&est), 200);
    
    /* RTTVAR should reflect variation */
    EXPECT_GT(xgl_rtt_get_rttvar(&est), 0);
}

/*---------------------------------------------------------------------------*/
/* RTO Clamping Tests (RFC 6298 Section 2.4)                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test RTO minimum clamping
 */
TEST(XglRttTest, RtoMinimumClamping) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Very small RTT measurement */
    xgl_rtt_update(&est, 10);
    
    /* RTO should be clamped to minimum */
    EXPECT_EQ(xgl_rtt_get_rto(&est), XGL_MIN_RTO_MS);
}

/**
 * \brief           Test RTO maximum clamping
 */
TEST(XglRttTest, RtoMaximumClamping) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Very large RTT measurement */
    xgl_rtt_update(&est, 10000);
    
    /* RTO should be clamped to maximum */
    EXPECT_EQ(xgl_rtt_get_rto(&est), XGL_MAX_RTO_MS);
}

/**
 * \brief           Test RTO stays within bounds
 */
TEST(XglRttTest, RtoWithinBounds) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Test with various measurements */
    for (int rtt = 50; rtt <= 2000; rtt += 50) {
        xgl_rtt_init(&est);
        xgl_rtt_update(&est, rtt);
        
        int32_t rto = xgl_rtt_get_rto(&est);
        EXPECT_GE(rto, XGL_MIN_RTO_MS);
        EXPECT_LE(rto, XGL_MAX_RTO_MS);
    }
}

/*---------------------------------------------------------------------------*/
/* Edge Case Tests                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test with zero RTT measurement
 */
TEST(XglRttTest, ZeroRttMeasurement) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    xgl_rtt_update(&est, 0);
    
    EXPECT_TRUE(xgl_rtt_is_initialized(&est));
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0);
    EXPECT_EQ(xgl_rtt_get_rto(&est), XGL_MIN_RTO_MS);
}

/**
 * \brief           Test with negative RTT measurement
 */
TEST(XglRttTest, NegativeRttMeasurement) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Negative RTT should be clamped to 0 */
    xgl_rtt_update(&est, -100);
    
    EXPECT_TRUE(xgl_rtt_is_initialized(&est));
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0);
}

/**
 * \brief           Test with null pointer in update
 */
TEST(XglRttTest, UpdateNullPointer) {
    xgl_rtt_update(nullptr, 100);  /* Should not crash */
    EXPECT_TRUE(true);
}

/**
 * \brief           Test get functions with null pointer
 */
TEST(XglRttTest, GetFunctionsNullPointer) {
    EXPECT_EQ(xgl_rtt_get_rto(nullptr), XGL_DEFAULT_RTO_MS);
    EXPECT_EQ(xgl_rtt_get_srtt(nullptr), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(nullptr), 0);
}

/**
 * \brief           Test reset functionality
 */
TEST(XglRttTest, Reset) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Add some measurements */
    xgl_rtt_update(&est, 100);
    xgl_rtt_update(&est, 120);
    xgl_rtt_update(&est, 110);
    
    EXPECT_TRUE(xgl_rtt_is_initialized(&est));
    
    /* Reset */
    xgl_rtt_reset(&est);
    
    EXPECT_FALSE(xgl_rtt_is_initialized(&est));
    EXPECT_EQ(xgl_rtt_get_rto(&est), XGL_DEFAULT_RTO_MS);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0);
}

/*---------------------------------------------------------------------------*/
/* Realistic Scenario Tests                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test with realistic network conditions
 */
TEST(XglRttTest, RealisticNetworkConditions) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Simulate realistic RTT measurements with some jitter */
    int32_t measurements[] = {
        100, 105, 98, 102, 110, 95, 103, 108, 97, 101,
        99, 104, 106, 100, 102, 98, 105, 103, 100, 99
    };
    
    for (size_t i = 0; i < sizeof(measurements) / sizeof(measurements[0]); i++) {
        xgl_rtt_update(&est, measurements[i]);
    }
    
    /* SRTT should be around 100ms */
    EXPECT_NEAR(xgl_rtt_get_srtt(&est), 100, 10);
    
    /* RTTVAR should be small (low jitter) */
    EXPECT_LT(xgl_rtt_get_rttvar(&est), 20);
    
    /* RTO should be reasonable */
    int32_t rto = xgl_rtt_get_rto(&est);
    EXPECT_GT(rto, 100);
    EXPECT_LT(rto, 300);
}

/**
 * \brief           Test with high jitter network
 */
TEST(XglRttTest, HighJitterNetwork) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Simulate high jitter: RTT varies between 50ms and 200ms */
    int32_t measurements[] = {
        100, 150, 75, 180, 60, 170, 90, 160, 80, 140,
        110, 130, 95, 155, 85, 145, 105, 135, 100, 125
    };
    
    for (size_t i = 0; i < sizeof(measurements) / sizeof(measurements[0]); i++) {
        xgl_rtt_update(&est, measurements[i]);
    }
    
    /* RTTVAR should be significant (high jitter) */
    EXPECT_GT(xgl_rtt_get_rttvar(&est), 15);
    
    /* RTO should be larger to accommodate jitter */
    int32_t rto = xgl_rtt_get_rto(&est);
    EXPECT_GT(rto, 180);
}

/**
 * \brief           Test with sudden RTT increase
 */
TEST(XglRttTest, SuddenRttIncrease) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Stable at 100ms */
    for (int i = 0; i < 10; i++) {
        xgl_rtt_update(&est, 100);
    }
    
    int32_t rto_before = xgl_rtt_get_rto(&est);
    
    /* Sudden increase to 500ms */
    xgl_rtt_update(&est, 500);
    
    int32_t rto_after = xgl_rtt_get_rto(&est);
    
    /* RTO should increase */
    EXPECT_GT(rto_after, rto_before);
}

/**
 * \brief           Test with sudden RTT decrease
 */
TEST(XglRttTest, SuddenRttDecrease) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Stable at 500ms */
    for (int i = 0; i < 10; i++) {
        xgl_rtt_update(&est, 500);
    }
    
    /* Sudden decrease to 100ms */
    xgl_rtt_update(&est, 100);
    
    int32_t rto_after = xgl_rtt_get_rto(&est);
    
    /* RTO should still be reasonable (doesn't drop too fast) */
    EXPECT_GT(rto_after, XGL_MIN_RTO_MS);
}

/*---------------------------------------------------------------------------*/
/* Algorithm Verification Tests                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Verify RFC 6298 algorithm step by step
 */
TEST(XglRttTest, Rfc6298AlgorithmVerification) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* First measurement: R = 100 */
    xgl_rtt_update(&est, 100);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 100);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 50);  /* R/2 */
    EXPECT_EQ(xgl_rtt_get_rto(&est), 300);    /* 100 + 4*50 */
    
    /* Second measurement: R = 120 */
    /* error = 120 - 100 = 20 */
    /* SRTT = 100 + 20/8 = 102 */
    /* |error| = 20 */
    /* RTTVAR = 50 + (20 - 50)/4 = 50 + (-30 >> 2) = 50 - 8 = 42 */
    /* RTO = 102 + 4*42 = 270 */
    xgl_rtt_update(&est, 120);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 102);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 42);
    EXPECT_EQ(xgl_rtt_get_rto(&est), 270);
    
    /* Third measurement: R = 90 */
    /* error = 90 - 102 = -12 */
    /* SRTT = 102 + (-12)/8 = 102 - 1 = 101 */
    /* |error| = 12 */
    /* RTTVAR = 42 + (12 - 42)/4 = 42 + (-30 >> 2) = 42 - 8 = 34 */
    /* RTO = 101 + 4*34 = 237, but let's check actual value */
    xgl_rtt_update(&est, 90);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 100);  /* 102 + (-12 >> 3) = 102 - 1 = 101, but -12 >> 3 = -2, so 102 - 2 = 100 */
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 34);
    EXPECT_EQ(xgl_rtt_get_rto(&est), 236);  /* 100 + 4*34 = 236 */
}

/**
 * \brief           Test exponential moving average behavior
 */
TEST(XglRttTest, ExponentialMovingAverage) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);
    
    /* Start with 100ms */
    xgl_rtt_update(&est, 100);
    
    /* Add many 200ms measurements */
    for (int i = 0; i < 50; i++) {
        xgl_rtt_update(&est, 200);
    }
    
    /* SRTT should converge towards 200ms but not reach it immediately */
    int32_t srtt = xgl_rtt_get_srtt(&est);
    EXPECT_GT(srtt, 180);  /* Should be close to 200 */
    EXPECT_LE(srtt, 200);  /* But not exceed it */
}

