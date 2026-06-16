/**
 * \file            test_transport_properties.cpp
 * \brief           Transport layer property tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include "property_framework.h"
#include <xgl/internal/xgl_rtt.h>
#include <xgl/internal/xgl_window.h>
#include <xgl/internal/xgl_reliable.h>
#include <xgl/xgl_types.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_wire.h>
#include <cmath>

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Calculate absolute value for int32_t
 */
static inline int32_t abs_int32(int32_t value) {
    return (value < 0) ? -value : value;
}

/**
 * \brief           Check if value is within tolerance
 */
static inline bool within_tolerance(int32_t actual, int32_t expected, int32_t tolerance) {
    return abs_int32(actual - expected) <= tolerance;
}

/*---------------------------------------------------------------------------*/
/* Property 18: RTT Estimation                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 18: RTT Estimation
 * \details         For any received ACK, the transport layer should update
 *                  the RTT estimate using exponential moving average
 *                  (SRTT += error/8, RTTVAR += (|error| - RTTVAR)/4).
 * \note            Validates: Requirements 6.1
 */
TEST(XglTransportProperties, Property18_RTTEstimation) {
    PropertyTestGenerator gen;

    /* Test with 100+ random RTT measurements */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_rtt_estimator_t est;
        xgl_rtt_init(&est);

        /* Generate random RTT measurement (1ms to 1000ms) */
        int32_t measured_rtt = 1 + (gen.random_uint32() % 1000);

        /* First measurement: SRTT = R, RTTVAR = R/2 */
        xgl_rtt_update(&est, measured_rtt);

        EXPECT_TRUE(xgl_rtt_is_initialized(&est))
            << "Estimator should be initialized after first measurement";

        EXPECT_EQ(xgl_rtt_get_srtt(&est), measured_rtt)
            << "First measurement: SRTT should equal measured RTT";

        EXPECT_EQ(xgl_rtt_get_rttvar(&est), measured_rtt / 2)
            << "First measurement: RTTVAR should equal measured RTT / 2";

        /* Subsequent measurements: test exponential moving average */
        int32_t prev_srtt = xgl_rtt_get_srtt(&est);
        int32_t prev_rttvar = xgl_rtt_get_rttvar(&est);

        /* Generate second measurement */
        int32_t measured_rtt2 = 1 + (gen.random_uint32() % 1000);
        xgl_rtt_update(&est, measured_rtt2);

        /* Calculate expected values using RFC 6298 algorithm */
        int32_t error = measured_rtt2 - prev_srtt;
        int32_t expected_srtt = prev_srtt + (error >> XGL_RTT_ALPHA_SHIFT);  /* error/8 */

        int32_t abs_error = abs_int32(error);
        int32_t rttvar_delta = abs_error - prev_rttvar;
        int32_t expected_rttvar = prev_rttvar + (rttvar_delta >> XGL_RTT_BETA_SHIFT);  /* delta/4 */

        /* Verify SRTT update follows RFC 6298 */
        EXPECT_EQ(xgl_rtt_get_srtt(&est), expected_srtt)
            << "SRTT should be updated using: SRTT += error/8";

        /* Verify RTTVAR update follows RFC 6298 */
        EXPECT_EQ(xgl_rtt_get_rttvar(&est), expected_rttvar)
            << "RTTVAR should be updated using: RTTVAR += (|error| - RTTVAR)/4";
    }
}

/**
 * \brief           Test RTT estimation with sequence of measurements
 * \details         Verifies that multiple measurements converge correctly
 */
TEST(XglTransportProperties, Property18_RTTEstimationSequence) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_rtt_estimator_t est;
        xgl_rtt_init(&est);

        /* Generate sequence of 5-10 measurements */
        int num_measurements = 5 + (gen.random_uint8() % 6);

        for (int i = 0; i < num_measurements; ++i) {
            int32_t prev_srtt = xgl_rtt_get_srtt(&est);
            int32_t prev_rttvar = xgl_rtt_get_rttvar(&est);
            bool was_initialized = xgl_rtt_is_initialized(&est);

            /* Generate random RTT (1ms to 500ms) */
            int32_t measured_rtt = 1 + (gen.random_uint32() % 500);
            xgl_rtt_update(&est, measured_rtt);

            /* Verify estimator is now initialized */
            EXPECT_TRUE(xgl_rtt_is_initialized(&est));

            if (!was_initialized) {
                /* First measurement */
                EXPECT_EQ(xgl_rtt_get_srtt(&est), measured_rtt);
                EXPECT_EQ(xgl_rtt_get_rttvar(&est), measured_rtt / 2);
            } else {
                /* Subsequent measurements - verify algorithm */
                int32_t error = measured_rtt - prev_srtt;
                int32_t expected_srtt = prev_srtt + (error >> XGL_RTT_ALPHA_SHIFT);

                int32_t abs_error = abs_int32(error);
                int32_t rttvar_delta = abs_error - prev_rttvar;
                int32_t expected_rttvar = prev_rttvar + (rttvar_delta >> XGL_RTT_BETA_SHIFT);

                EXPECT_EQ(xgl_rtt_get_srtt(&est), expected_srtt);
                EXPECT_EQ(xgl_rtt_get_rttvar(&est), expected_rttvar);
            }
        }
    }
}

/**
 * \brief           Test RTT estimation with edge cases
 * \details         Tests boundary conditions and special values
 */
TEST(XglTransportProperties, Property18_RTTEstimationEdgeCases) {
    xgl_rtt_estimator_t est;

    /* Test with zero RTT */
    xgl_rtt_init(&est);
    xgl_rtt_update(&est, 0);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0);

    /* Test with very small RTT */
    xgl_rtt_init(&est);
    xgl_rtt_update(&est, 1);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 1);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0);  /* 1/2 = 0 in integer division */

    /* Test with very large RTT */
    xgl_rtt_init(&est);
    xgl_rtt_update(&est, 10000);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 10000);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 5000);

    /* Test with negative RTT (should be clamped to 0) */
    xgl_rtt_init(&est);
    xgl_rtt_update(&est, -100);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0);
}

/*---------------------------------------------------------------------------*/
/* Property 19: RTO Calculation                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 19: RTO Calculation
 * \details         For any RTT estimate, the calculated RTO should equal
 *                  SRTT + 4 * RTTVAR, clamped to [MIN_RTO, MAX_RTO].
 * \note            Validates: Requirements 6.2
 */
TEST(XglTransportProperties, Property19_ROCalculation) {
    PropertyTestGenerator gen;

    /* Test with 100+ random RTT measurements */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_rtt_estimator_t est;
        xgl_rtt_init(&est);

        /* Before initialization, should return default RTO */
        EXPECT_EQ(xgl_rtt_get_rto(&est), XGL_DEFAULT_RTO_MS)
            << "Uninitialized estimator should return default RTO";

        /* Generate random RTT measurement (1ms to 2000ms) */
        int32_t measured_rtt = 1 + (gen.random_uint32() % 2000);
        xgl_rtt_update(&est, measured_rtt);

        /* Calculate expected RTO: SRTT + 4 * RTTVAR */
        int32_t srtt = xgl_rtt_get_srtt(&est);
        int32_t rttvar = xgl_rtt_get_rttvar(&est);
        int32_t expected_rto = srtt + (XGL_RTO_K_FACTOR * rttvar);

        /* Clamp to [MIN_RTO, MAX_RTO] */
        if (expected_rto < XGL_MIN_RTO_MS) {
            expected_rto = XGL_MIN_RTO_MS;
        }
        if (expected_rto > XGL_MAX_RTO_MS) {
            expected_rto = XGL_MAX_RTO_MS;
        }

        int32_t actual_rto = xgl_rtt_get_rto(&est);

        EXPECT_EQ(actual_rto, expected_rto)
            << "RTO should equal SRTT + 4 * RTTVAR, clamped to [MIN_RTO, MAX_RTO]"
            << "\n  SRTT: " << srtt
            << "\n  RTTVAR: " << rttvar
            << "\n  Expected RTO: " << expected_rto
            << "\n  Actual RTO: " << actual_rto;

        /* Verify RTO is within bounds */
        EXPECT_GE(actual_rto, XGL_MIN_RTO_MS)
            << "RTO should be >= MIN_RTO_MS (" << XGL_MIN_RTO_MS << ")";

        EXPECT_LE(actual_rto, XGL_MAX_RTO_MS)
            << "RTO should be <= MAX_RTO_MS (" << XGL_MAX_RTO_MS << ")";
    }
}

/**
 * \brief           Test RTO calculation with multiple measurements
 * \details         Verifies RTO updates correctly after each measurement
 */
TEST(XglTransportProperties, Property19_ROCalculationSequence) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_rtt_estimator_t est;
        xgl_rtt_init(&est);

        /* Generate sequence of measurements */
        int num_measurements = 3 + (gen.random_uint8() % 8);

        for (int i = 0; i < num_measurements; ++i) {
            /* Generate random RTT */
            int32_t measured_rtt = 1 + (gen.random_uint32() % 1500);
            xgl_rtt_update(&est, measured_rtt);

            /* Verify RTO calculation */
            int32_t srtt = xgl_rtt_get_srtt(&est);
            int32_t rttvar = xgl_rtt_get_rttvar(&est);
            int32_t expected_rto = srtt + (XGL_RTO_K_FACTOR * rttvar);

            /* Apply clamping */
            if (expected_rto < XGL_MIN_RTO_MS) {
                expected_rto = XGL_MIN_RTO_MS;
            }
            if (expected_rto > XGL_MAX_RTO_MS) {
                expected_rto = XGL_MAX_RTO_MS;
            }

            EXPECT_EQ(xgl_rtt_get_rto(&est), expected_rto)
                << "RTO calculation failed at measurement " << i;
        }
    }
}

/**
 * \brief           Test RTO clamping to minimum value
 * \details         Verifies RTO is clamped to MIN_RTO when calculated value is too small
 */
TEST(XglTransportProperties, Property19_ROClampingMinimum) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);

    /* Use very small RTT that would result in RTO < MIN_RTO */
    xgl_rtt_update(&est, 10);  /* SRTT=10, RTTVAR=5, RTO=10+4*5=30 */

    int32_t rto = xgl_rtt_get_rto(&est);

    /* RTO should be clamped to MIN_RTO_MS (100) */
    EXPECT_EQ(rto, XGL_MIN_RTO_MS)
        << "RTO should be clamped to MIN_RTO_MS when calculated value is too small";
}

/**
 * \brief           Test RTO clamping to maximum value
 * \details         Verifies RTO is clamped to MAX_RTO when calculated value is too large
 */
TEST(XglTransportProperties, Property19_ROClampingMaximum) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);

    /* Use very large RTT that would result in RTO > MAX_RTO */
    xgl_rtt_update(&est, 5000);  /* SRTT=5000, RTTVAR=2500, RTO=5000+4*2500=15000 */

    int32_t rto = xgl_rtt_get_rto(&est);

    /* RTO should be clamped to MAX_RTO_MS (5000) */
    EXPECT_EQ(rto, XGL_MAX_RTO_MS)
        << "RTO should be clamped to MAX_RTO_MS when calculated value is too large";
}

/**
 * \brief           Test RTO with stable RTT
 * \details         When RTT is stable, RTO should converge to a stable value
 */
TEST(XglTransportProperties, Property19_ROStableRTT) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);

    /* Feed stable RTT measurements */
    const int32_t stable_rtt = 200;

    for (int i = 0; i < 10; ++i) {
        xgl_rtt_update(&est, stable_rtt);
    }

    /* After many stable measurements, SRTT should converge to measured RTT */
    int32_t srtt = xgl_rtt_get_srtt(&est);
    EXPECT_TRUE(within_tolerance(srtt, stable_rtt, 10))
        << "SRTT should converge to stable RTT value";

    /* RTTVAR should converge to near zero */
    int32_t rttvar = xgl_rtt_get_rttvar(&est);
    EXPECT_LT(rttvar, 20)
        << "RTTVAR should be small with stable RTT";

    /* RTO should be close to SRTT when variation is low */
    int32_t rto = xgl_rtt_get_rto(&est);
    int32_t expected_rto = srtt + (XGL_RTO_K_FACTOR * rttvar);
    if (expected_rto < XGL_MIN_RTO_MS) expected_rto = XGL_MIN_RTO_MS;
    if (expected_rto > XGL_MAX_RTO_MS) expected_rto = XGL_MAX_RTO_MS;

    EXPECT_EQ(rto, expected_rto)
        << "RTO should match calculated value with stable RTT";
}

/**
 * \brief           Test RTO with varying RTT
 * \details         When RTT varies, RTTVAR should increase and RTO should adapt
 */
TEST(XglTransportProperties, Property19_ROVaryingRTT) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);

    /* Feed varying RTT measurements */
    int32_t rtts[] = {100, 200, 150, 300, 100, 250, 180};
    int num_rtts = sizeof(rtts) / sizeof(rtts[0]);

    for (int i = 0; i < num_rtts; ++i) {
        xgl_rtt_update(&est, rtts[i]);

        /* Verify RTO is always within bounds */
        int32_t rto = xgl_rtt_get_rto(&est);
        EXPECT_GE(rto, XGL_MIN_RTO_MS);
        EXPECT_LE(rto, XGL_MAX_RTO_MS);

        /* Verify RTO calculation */
        int32_t srtt = xgl_rtt_get_srtt(&est);
        int32_t rttvar = xgl_rtt_get_rttvar(&est);
        int32_t expected_rto = srtt + (XGL_RTO_K_FACTOR * rttvar);
        if (expected_rto < XGL_MIN_RTO_MS) expected_rto = XGL_MIN_RTO_MS;
        if (expected_rto > XGL_MAX_RTO_MS) expected_rto = XGL_MAX_RTO_MS;

        EXPECT_EQ(rto, expected_rto);
    }
}

/*---------------------------------------------------------------------------*/
/* Additional RTT Tests                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test RTT reset functionality
 * \details         Verifies that reset returns estimator to initial state
 */
TEST(XglTransportProperties, RTTReset) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);

    /* Update with some measurements */
    xgl_rtt_update(&est, 100);
    xgl_rtt_update(&est, 150);
    xgl_rtt_update(&est, 200);

    EXPECT_TRUE(xgl_rtt_is_initialized(&est));

    /* Reset */
    xgl_rtt_reset(&est);

    /* Should be back to initial state */
    EXPECT_FALSE(xgl_rtt_is_initialized(&est));
    EXPECT_EQ(xgl_rtt_get_rto(&est), XGL_DEFAULT_RTO_MS);
    EXPECT_EQ(xgl_rtt_get_srtt(&est), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0);
}

/**
 * \brief           Test NULL pointer handling
 * \details         Verifies functions handle NULL pointers gracefully
 */
TEST(XglTransportProperties, RTTNullPointerHandling) {
    /* All functions should handle NULL gracefully */
    xgl_rtt_init(NULL);  /* Should not crash */
    xgl_rtt_update(NULL, 100);  /* Should not crash */
    xgl_rtt_reset(NULL);  /* Should not crash */

    EXPECT_EQ(xgl_rtt_get_rto(NULL), XGL_DEFAULT_RTO_MS);
    EXPECT_EQ(xgl_rtt_get_srtt(NULL), 0);
    EXPECT_EQ(xgl_rtt_get_rttvar(NULL), 0);
    EXPECT_FALSE(xgl_rtt_is_initialized(NULL));
}

/*---------------------------------------------------------------------------*/
/* Property 23: Sliding Window Maintenance                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 23: Sliding Window Maintenance
 * \details         The production sliding window is maintained by 32-bit packet
 *                  numbers and must never depend on 8-bit sequence wraparound.
 * \note            Validates: Requirements 7.5
 */
TEST(XglTransportProperties, Property23_SlidingWindowMaintenance) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        uint8_t window_size = 1 + (gen.random_uint8() % 128);

        xgl_sliding_window_t window;
        xgl_error_t err = xgl_window_init(&window, window_size);
        ASSERT_EQ(err, XGL_OK) << "Window initialization failed";

        int num_operations = 10 + (gen.random_uint8() % 50);
        for (int op = 0; op < num_operations; ++op) {
            bool do_send = (gen.random_uint8() % 100) < 70;

            if (do_send && xgl_window_can_send_packet_number(&window)) {
                uint32_t before = xgl_window_get_next_packet_number(&window);
                xgl_window_advance_next_packet_number(&window);
                EXPECT_EQ(xgl_window_get_next_packet_number(&window), before + 1U)
                    << "Packet number should advance monotonically";
            } else if (!do_send && xgl_window_get_usage(&window) > 0U) {
                uint32_t outstanding = window.next_packet_number -
                                       window.send_base_packet_number;
                uint32_t offset = gen.random_uint32() % outstanding;
                uint32_t packet_to_ack = window.send_base_packet_number + offset;

                err = xgl_window_mark_ack_packet_number(&window, packet_to_ack);
                EXPECT_EQ(err, XGL_OK)
                    << "Marking ACK should succeed for in-window packet";
                (void)xgl_window_advance_base_packet_number(&window);
            }

            uint32_t usage = window.next_packet_number - window.send_base_packet_number;
            EXPECT_LE(usage, window_size)
                << "Window invariant violated after operation " << op
                << "\n  send_base_packet_number: " << window.send_base_packet_number
                << "\n  next_packet_number: " << window.next_packet_number
                << "\n  window_size: " << (int)window_size
                << "\n  usage: " << usage;

            EXPECT_EQ(xgl_window_can_send(&window), usage < window_size)
                << "can_send should be true iff packet-number usage < window_size";
        }

        xgl_window_destroy(&window);
    }
}

TEST(XglTransportProperties, Property23_SlidingWindowMaxSize) {
    xgl_sliding_window_t window;
    xgl_error_t err = xgl_window_init(&window, 128);
    ASSERT_EQ(err, XGL_OK);

    for (int i = 0; i < 128; ++i) {
        EXPECT_TRUE(xgl_window_can_send(&window));
        xgl_window_advance_next_packet_number(&window);
    }

    EXPECT_FALSE(xgl_window_can_send(&window));
    EXPECT_EQ(xgl_window_get_usage(&window), 128);

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowDoesNotWrapAtEightBits) {
    xgl_sliding_window_t window;
    uint8_t window_size = 16;
    xgl_error_t err = xgl_window_init(&window, window_size);
    ASSERT_EQ(err, XGL_OK);

    window.send_base_packet_number = 250U;
    window.next_packet_number = 250U;

    for (int i = 0; i < window_size; ++i) {
        ASSERT_TRUE(xgl_window_can_send_packet_number(&window));
        xgl_window_advance_next_packet_number(&window);
    }

    EXPECT_EQ(window.next_packet_number, 266U);
    EXPECT_FALSE(xgl_window_can_send_packet_number(&window));
    EXPECT_TRUE(xgl_window_is_in_window_packet_number(&window, 250U));
    EXPECT_TRUE(xgl_window_is_in_window_packet_number(&window, 265U));
    EXPECT_FALSE(xgl_window_is_in_window_packet_number(&window, 266U));

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowAllAcked) {
    xgl_sliding_window_t window;
    uint8_t window_size = 8;
    xgl_error_t err = xgl_window_init(&window, window_size);
    ASSERT_EQ(err, XGL_OK);

    for (int i = 0; i < window_size; ++i) {
        xgl_window_advance_next_packet_number(&window);
    }

    for (uint32_t packet_number = 0; packet_number < window_size; ++packet_number) {
        err = xgl_window_mark_ack_packet_number(&window, packet_number);
        EXPECT_EQ(err, XGL_OK);
    }

    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), window_size);
    EXPECT_EQ(xgl_window_get_usage(&window), 0);
    EXPECT_TRUE(xgl_window_can_send(&window));

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowOutOfOrderAcks) {
    xgl_sliding_window_t window;
    uint8_t window_size = 8;
    xgl_error_t err = xgl_window_init(&window, window_size);
    ASSERT_EQ(err, XGL_OK);

    for (int i = 0; i < window_size; ++i) {
        xgl_window_advance_next_packet_number(&window);
    }

    const uint32_t ack_order[] = {2, 4, 6, 0, 1, 3, 5, 7};
    for (uint32_t packet_number : ack_order) {
        err = xgl_window_mark_ack_packet_number(&window, packet_number);
        EXPECT_EQ(err, XGL_OK);
        (void)xgl_window_advance_base_packet_number(&window);

        uint32_t usage = window.next_packet_number - window.send_base_packet_number;
        EXPECT_LE(usage, window_size);
    }

    EXPECT_EQ(xgl_window_get_usage(&window), 0);

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowReset) {
    PropertyTestGenerator gen;

    xgl_sliding_window_t window;
    uint8_t window_size = 16;
    xgl_error_t err = xgl_window_init(&window, window_size);
    ASSERT_EQ(err, XGL_OK);

    for (int i = 0; i < 50; ++i) {
        if (xgl_window_can_send_packet_number(&window)) {
            xgl_window_advance_next_packet_number(&window);
        }

        if ((gen.random_uint8() % 3) == 0 && xgl_window_get_usage(&window) > 0U) {
            uint32_t outstanding = window.next_packet_number -
                                   window.send_base_packet_number;
            uint32_t packet_number =
                window.send_base_packet_number + (gen.random_uint32() % outstanding);
            (void)xgl_window_mark_ack_packet_number(&window, packet_number);
            (void)xgl_window_advance_base_packet_number(&window);
        }
    }

    xgl_window_reset(&window);

    EXPECT_EQ(window.send_base_packet_number, 0U);
    EXPECT_EQ(window.next_packet_number, 0U);
    EXPECT_EQ(xgl_window_get_usage(&window), 0);
    EXPECT_TRUE(xgl_window_can_send(&window));

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowMinSize) {
    xgl_sliding_window_t window;
    xgl_error_t err = xgl_window_init(&window, 1);
    ASSERT_EQ(err, XGL_OK);

    EXPECT_TRUE(xgl_window_can_send(&window));
    xgl_window_advance_next_packet_number(&window);
    EXPECT_FALSE(xgl_window_can_send(&window));
    EXPECT_EQ(xgl_window_get_usage(&window), 1);

    err = xgl_window_mark_ack_packet_number(&window, 0U);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(xgl_window_advance_base(&window), 1);
    EXPECT_TRUE(xgl_window_can_send(&window));
    EXPECT_EQ(xgl_window_get_usage(&window), 0);

    xgl_window_destroy(&window);
}

/*---------------------------------------------------------------------------*/
/* Property 13: Reliable Transmission Queuing                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 13: Reliable Transmission Queuing
 * \details         For any packet sent with reliable transmission enabled,
 *                  the transport layer should add it to the wait-ACK queue.
 * \note            Validates: Requirements 5.1
 */
TEST(XglTransportProperties, Property13_ReliableTransmissionQueuing) {
    PropertyTestGenerator gen;

    /* Mock PHY operations */
    xgl_phy_ops_t phy;
    phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
        (void)data; (void)len; (void)user_data;
        return XGL_OK;
    };
    phy.rx = nullptr;
    phy.user_data = nullptr;

    /* Test with 100+ random packet configurations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, 5, nullptr);
        ASSERT_EQ(err, XGL_OK) << "Queue initialization failed";

        /* Verify queue starts empty */
        EXPECT_TRUE(xgl_reliable_is_empty(&queue))
            << "Queue should be empty initially";
        EXPECT_EQ(xgl_reliable_get_count(&queue), 0)
            << "Queue count should be 0 initially";

        /* Generate random packet data */
        size_t data_len = 1 + (gen.random_uint8() % 255);
        std::vector<uint8_t> data = gen.random_bytes(data_len);

        uint16_t source_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        uint16_t target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        uint32_t packet_number = gen.random_uint32();
        uint8_t data_type = gen.random_uint8();
        uint8_t priority = gen.random_uint8() % 8;
        int32_t timeout_ms = 100 + (gen.random_uint32() % 5000);

        /* Add packet to queue */
        err = xgl_reliable_add_packet_number(&queue, data.data(), data_len,
                                     source_id, target_id, packet_number,
                                     data_type, priority, timeout_ms, &phy);

        EXPECT_EQ(err, XGL_OK)
            << "Adding packet to queue should succeed";

        /* Verify packet was added to queue */
        EXPECT_FALSE(xgl_reliable_is_empty(&queue))
            << "Queue should not be empty after adding packet";

        EXPECT_EQ(xgl_reliable_get_count(&queue), 1)
            << "Queue count should be 1 after adding one packet";

        /* Verify packet can be found in queue */
        xgl_reliable_packet_t* found = xgl_reliable_find_packet_number(&queue, packet_number, target_id);
        ASSERT_NE(found, nullptr)
            << "Packet should be findable in queue";

        /* Verify packet data matches */
        EXPECT_EQ(found->packet_number, packet_number);
        EXPECT_EQ(found->target_id, target_id);
        EXPECT_EQ(found->source_id, source_id);
        EXPECT_EQ(found->data_type, data_type);
        EXPECT_EQ(found->priority, priority);
        EXPECT_EQ(found->data_len, data_len);
        EXPECT_EQ(found->initial_timeout_ms, timeout_ms);
        EXPECT_EQ(found->timeout_ms, timeout_ms);
        EXPECT_EQ(found->retry_count, 0);

        /* Clean up */
        xgl_reliable_destroy(&queue);
    }
}

/**
 * \brief           Test queuing multiple packets
 * \details         Verifies multiple packets can be queued correctly
 */
TEST(XglTransportProperties, Property13_ReliableTransmissionQueuingMultiple) {
    PropertyTestGenerator gen;

    xgl_phy_ops_t phy;
    phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
        (void)data; (void)len; (void)user_data;
        return XGL_OK;
    };
    phy.rx = nullptr;
    phy.user_data = nullptr;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, 5, nullptr);
        ASSERT_EQ(err, XGL_OK);

        /* Add random number of packets (1-20) */
        int num_packets = 1 + (gen.random_uint8() % 20);

        for (int i = 0; i < num_packets; ++i) {
            std::vector<uint8_t> data = gen.random_bytes(10 + (gen.random_uint8() % 100));

            err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                         static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U),
                                         static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U),
                                         static_cast<uint32_t>(i),
                                         gen.random_uint8(),
                                         gen.random_uint8() % 8, 1000, &phy);

            EXPECT_EQ(err, XGL_OK)
                << "Adding packet " << i << " should succeed";

            /* Verify count increases */
            EXPECT_EQ(xgl_reliable_get_count(&queue), (size_t)(i + 1))
                << "Queue count should match number of packets added";
        }

        /* Verify final count */
        EXPECT_EQ(xgl_reliable_get_count(&queue), (size_t)num_packets);
        EXPECT_FALSE(xgl_reliable_is_empty(&queue));

        xgl_reliable_destroy(&queue);
    }
}

/*---------------------------------------------------------------------------*/
/* Property 14: Retransmission on Timeout                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 14: Retransmission on Timeout
 * \details         For any packet in the wait-ACK queue, if no ACK is received
 *                  within the timeout period, the packet should be retransmitted.
 * \note            Validates: Requirements 5.2
 */
TEST(XglTransportProperties, Property14_RetransmissionOnTimeout) {
    PropertyTestGenerator gen;

    /* Track transmissions */
    struct TxTracker {
        int tx_count = 0;
        std::vector<uint8_t> last_data;
    };

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        TxTracker tracker;

        /* Mock PHY that tracks transmissions */
        xgl_phy_ops_t phy;
        phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
            TxTracker* t = static_cast<TxTracker*>(user_data);
            t->tx_count++;
            t->last_data.assign(data, data + len);
            return XGL_OK;
        };
        phy.rx = nullptr;
        phy.user_data = &tracker;

        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, 5, nullptr);
        ASSERT_EQ(err, XGL_OK);

        /* Generate random packet */
        std::vector<uint8_t> data = gen.random_bytes(10 + (gen.random_uint8() % 50));
        uint32_t packet_number = gen.random_uint32();
        uint16_t target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        int32_t timeout_ms = 100 + (gen.random_uint32() % 500);

        /* Add packet to queue */
        err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                     1, target_id, packet_number, 0, 0, timeout_ms, &phy);
        ASSERT_EQ(err, XGL_OK);

        /* Set initial send timestamp */
        xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, packet_number, target_id);
        ASSERT_NE(packet, nullptr);
        packet->send_timestamp = 1000;  /* Start time */

        /* Process timeouts before timeout expires - should not retransmit */
        uint32_t retx_count = xgl_reliable_process_timeouts(&queue,
                                                            1000 + timeout_ms - 1,
                                                            nullptr);
        EXPECT_EQ(retx_count, 0)
            << "Should not retransmit before timeout expires";
        EXPECT_EQ(tracker.tx_count, 0)
            << "No transmission should occur before timeout";

        /* Process timeouts after timeout expires - should retransmit */
        retx_count = xgl_reliable_process_timeouts(&queue,
                                                   1000 + timeout_ms,
                                                   nullptr);
        EXPECT_EQ(retx_count, 1)
            << "Should retransmit exactly once when timeout expires";
        EXPECT_EQ(tracker.tx_count, 1)
            << "PHY tx should be called once for retransmission";

        /* Verify packet is still in queue */
        EXPECT_EQ(xgl_reliable_get_count(&queue), 1)
            << "Packet should remain in queue after retransmission";

        /* Verify retry count incremented */
        packet = xgl_reliable_find_packet_number(&queue, packet_number, target_id);
        ASSERT_NE(packet, nullptr);
        EXPECT_EQ(packet->retry_count, 1)
            << "Retry count should be incremented after retransmission";

        xgl_reliable_destroy(&queue);
    }
}

/**
 * \brief           Test multiple retransmissions
 * \details         Verifies packet is retransmitted multiple times on repeated timeouts
 */
TEST(XglTransportProperties, Property14_RetransmissionMultiple) {
    PropertyTestGenerator gen;

    struct TxTracker {
        int tx_count = 0;
    };

    TxTracker tracker;

    xgl_phy_ops_t phy;
    phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
        (void)data; (void)len;
        TxTracker* t = static_cast<TxTracker*>(user_data);
        t->tx_count++;
        return XGL_OK;
    };
    phy.rx = nullptr;
    phy.user_data = &tracker;

    xgl_reliable_queue_t queue;
    xgl_error_t err = xgl_reliable_init(&queue, 5, nullptr);
    ASSERT_EQ(err, XGL_OK);

    /* Add packet */
    std::vector<uint8_t> data = gen.random_bytes(20);
    err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                 1, 2, 10, 0, 0, 100, &phy);
    ASSERT_EQ(err, XGL_OK);

    /* Set initial timestamp */
    xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
    ASSERT_NE(packet, nullptr);
    packet->send_timestamp = 1000;

    /* Simulate 3 timeouts */
    uint32_t current_time = 1000;
    for (int i = 0; i < 3; ++i) {
        /* Advance time past timeout */
        current_time += packet->timeout_ms;

        uint32_t retx = xgl_reliable_process_timeouts(&queue, current_time, nullptr);
        EXPECT_EQ(retx, 1) << "Should retransmit on timeout " << i;

        /* Verify retry count */
        packet = xgl_reliable_find_packet_number(&queue, 10, 2);
        ASSERT_NE(packet, nullptr);
        EXPECT_EQ(packet->retry_count, (uint8_t)(i + 1));
    }

    /* Verify total transmissions */
    EXPECT_EQ(tracker.tx_count, 3);

    xgl_reliable_destroy(&queue);
}

/*---------------------------------------------------------------------------*/
/* Property 15: Retry Exhaustion Handling                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 15: Retry Exhaustion Handling
 * \details         For any packet that exceeds maximum retry count, the transport
 *                  layer should invoke the error callback and remove the packet
 *                  from the queue.
 * \note            Validates: Requirements 5.3
 */
TEST(XglTransportProperties, Property15_RetryExhaustionHandling) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_phy_ops_t phy;
        phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
            (void)data; (void)len; (void)user_data;
            return XGL_OK;
        };
        phy.rx = nullptr;
        phy.user_data = nullptr;

        /* Random max retry count (1-10) */
        uint8_t max_retry = 1 + (gen.random_uint8() % 10);

        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, max_retry, nullptr);
        ASSERT_EQ(err, XGL_OK);

        /* Add packet */
        std::vector<uint8_t> data = gen.random_bytes(10 + (gen.random_uint8() % 50));
        uint32_t packet_number = gen.random_uint32();
        uint16_t target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);

        err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                     1, target_id, packet_number, 0, 0, 100, &phy);
        ASSERT_EQ(err, XGL_OK);

        /* Set initial timestamp */
        xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, packet_number, target_id);
        ASSERT_NE(packet, nullptr);
        packet->send_timestamp = 1000;

        /* Exhaust retries */
        uint32_t current_time = 1000;
        for (uint8_t i = 0; i < max_retry; ++i) {
            current_time += packet->timeout_ms;
            xgl_reliable_process_timeouts(&queue, current_time, nullptr);

            /* Packet should still be in queue */
            EXPECT_EQ(xgl_reliable_get_count(&queue), 1)
                << "Packet should remain in queue until max retries exceeded";

            packet = xgl_reliable_find_packet_number(&queue, packet_number, target_id);
            ASSERT_NE(packet, nullptr);
        }

        /* One more timeout should remove packet */
        current_time += packet->timeout_ms;
        xgl_reliable_packet_t* exhausted = nullptr;
        xgl_reliable_process_timeouts(&queue, current_time, &exhausted);

        /* Packet should be removed from queue */
        EXPECT_EQ(xgl_reliable_get_count(&queue), 0)
            << "Packet should be removed after max retries exceeded";

        EXPECT_TRUE(xgl_reliable_is_empty(&queue))
            << "Queue should be empty after retry exhaustion";

        /* Verify packet was returned as exhausted */
        ASSERT_NE(exhausted, nullptr)
            << "Exhausted packet should be returned to caller";

        EXPECT_EQ(exhausted->packet_number, packet_number);
        EXPECT_EQ(exhausted->target_id, target_id);
        EXPECT_EQ(exhausted->retry_count, max_retry);

        /* Clean up exhausted packet */
        if (exhausted != nullptr && exhausted->data != nullptr) {
            free(exhausted->data);
        }
        free(exhausted);

        xgl_reliable_destroy(&queue);
    }
}

/**
 * \brief           Test retry exhaustion with multiple packets
 * \details         Verifies only exhausted packets are removed
 */
TEST(XglTransportProperties, Property15_RetryExhaustionMultiplePackets) {
    PropertyTestGenerator gen;

    xgl_phy_ops_t phy;
    phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
        (void)data; (void)len; (void)user_data;
        return XGL_OK;
    };
    phy.rx = nullptr;
    phy.user_data = nullptr;

    xgl_reliable_queue_t queue;
    xgl_error_t err = xgl_reliable_init(&queue, 3, nullptr);
    ASSERT_EQ(err, XGL_OK);

    /* Add 3 packets - only set timestamp for first one */
    for (int i = 0; i < 3; ++i) {
        std::vector<uint8_t> data = gen.random_bytes(20);
        err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                     1, 2, (uint8_t)i, 0, 0, 100, &phy);
        ASSERT_EQ(err, XGL_OK);
    }

    /* Only set timestamp for first packet - others have timestamp=0 (not sent yet) */
    xgl_reliable_packet_t* packet0 = xgl_reliable_find_packet_number(&queue, 0, 2);
    ASSERT_NE(packet0, nullptr);
    packet0->send_timestamp = 1000;

    EXPECT_EQ(xgl_reliable_get_count(&queue), 3);

    /* Exhaust first packet only by processing timeouts */
    /* Packet starts at 1000ms with 100ms timeout */
    /* After 3 retries with exponential backoff: 100, 200, 400, 800 */

    uint32_t current_time = 1000;

    /* Trigger retries until exhaustion */
    for (int retry = 0; retry <= 3; ++retry) {
        /* Advance time past current timeout */
        current_time += packet0->timeout_ms;

        xgl_reliable_packet_t* exhausted = nullptr;
        xgl_reliable_process_timeouts(&queue, current_time, &exhausted);

        if (retry < 3) {
            /* Should still be in queue */
            EXPECT_EQ(xgl_reliable_get_count(&queue), 3)
                << "All packets should remain during retries (retry " << retry << ")";
            EXPECT_EQ(exhausted, nullptr)
                << "No packet should be exhausted yet (retry " << retry << ")";

            /* Update packet pointer after processing */
            packet0 = xgl_reliable_find_packet_number(&queue, 0, 2);
            ASSERT_NE(packet0, nullptr)
                << "Packet 0 should still exist after retry " << retry;
        } else {
            /* Should be removed */
            EXPECT_NE(exhausted, nullptr)
                << "Packet should be exhausted after max retries";

            if (exhausted != nullptr) {
                EXPECT_EQ(exhausted->packet_number, 0)
                    << "First packet should be exhausted";
                free(exhausted->data);
                free(exhausted);
            }
        }
    }

    /* First packet should be removed, others remain (they were never sent) */
    EXPECT_EQ(xgl_reliable_get_count(&queue), 2)
        << "Only exhausted packet should be removed";

    EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 0, 2), nullptr)
        << "Packet 0 should be removed";
    EXPECT_NE(xgl_reliable_find_packet_number(&queue, 1, 2), nullptr)
        << "Packet 1 should still exist (never sent)";
    EXPECT_NE(xgl_reliable_find_packet_number(&queue, 2, 2), nullptr)
        << "Packet 2 should still exist (never sent)";

    xgl_reliable_destroy(&queue);
}

/*---------------------------------------------------------------------------*/
/* Property 20: Exponential Backoff                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 20: Exponential Backoff
 * \details         For any packet that is retransmitted multiple times,
 *                  the timeout should increase exponentially with each retry.
 * \note            Validates: Requirements 6.4
 */
TEST(XglTransportProperties, Property20_ExponentialBackoff) {
    PropertyTestGenerator gen;

    /* Test with 100+ random initial timeouts */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        /* Generate random initial timeout (50ms to 2000ms) */
        int32_t initial_timeout = 50 + (gen.random_uint32() % 1950);

        /* Test backoff calculation for retry counts 0-10 */
        int32_t prev_timeout = initial_timeout;

        for (uint8_t retry = 1; retry <= 10; ++retry) {
            int32_t backoff_timeout = xgl_reliable_calc_backoff(initial_timeout, retry);

            /* Verify exponential growth: timeout should double each retry */
            int32_t expected_timeout = initial_timeout * (1 << retry);  /* 2^retry */

            /* Cap at 30000ms */
            if (expected_timeout > 30000) {
                expected_timeout = 30000;
            }

            EXPECT_EQ(backoff_timeout, expected_timeout)
                << "Backoff timeout should follow exponential pattern"
                << "\n  initial_timeout: " << initial_timeout
                << "\n  retry: " << (int)retry
                << "\n  expected: " << expected_timeout
                << "\n  actual: " << backoff_timeout;

            /* Verify timeout increases (or stays at cap) */
            EXPECT_GE(backoff_timeout, prev_timeout)
                << "Timeout should never decrease with more retries";

            /* Verify timeout doesn't exceed maximum */
            EXPECT_LE(backoff_timeout, 30000)
                << "Timeout should be capped at 30000ms";

            prev_timeout = backoff_timeout;
        }
    }
}

/**
 * \brief           Test exponential backoff in queue processing
 * \details         Verifies timeout increases in actual queue operations
 */
TEST(XglTransportProperties, Property20_ExponentialBackoffInQueue) {
    PropertyTestGenerator gen;

    xgl_phy_ops_t phy;
    phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
        (void)data; (void)len; (void)user_data;
        return XGL_OK;
    };
    phy.rx = nullptr;
    phy.user_data = nullptr;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, 10, nullptr);
        ASSERT_EQ(err, XGL_OK);

        /* Random initial timeout */
        int32_t initial_timeout = 100 + (gen.random_uint32() % 500);

        /* Add packet */
        std::vector<uint8_t> data = gen.random_bytes(20);
        err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                     1, 2, 10, 0, 0, initial_timeout, &phy);
        ASSERT_EQ(err, XGL_OK);

        /* Set initial timestamp */
        xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, 10, 2);
        ASSERT_NE(packet, nullptr);
        packet->send_timestamp = 1000;

        /* Track timeout progression */
        std::vector<int32_t> timeouts;
        timeouts.push_back(packet->timeout_ms);

        /* Trigger 5 retransmissions */
        uint32_t current_time = 1000;
        for (int i = 0; i < 5; ++i) {
            current_time += packet->timeout_ms;
            xgl_reliable_process_timeouts(&queue, current_time, nullptr);

            packet = xgl_reliable_find_packet_number(&queue, 10, 2);
            ASSERT_NE(packet, nullptr);

            timeouts.push_back(packet->timeout_ms);

            /* Verify timeout increased */
            EXPECT_GT(packet->timeout_ms, timeouts[i])
                << "Timeout should increase after retry " << i;

            /* Verify exponential pattern */
            int32_t expected = xgl_reliable_calc_backoff(initial_timeout, packet->retry_count);
            EXPECT_EQ(packet->timeout_ms, expected)
                << "Timeout should match exponential backoff calculation";
        }

        /* Verify exponential growth pattern */
        for (size_t i = 1; i < timeouts.size(); ++i) {
            EXPECT_GE(timeouts[i], timeouts[i-1])
                << "Timeouts should be monotonically increasing";
        }

        xgl_reliable_destroy(&queue);
    }
}

/**
 * \brief           Test backoff with edge cases
 * \details         Verifies backoff handles boundary conditions correctly
 */
TEST(XglTransportProperties, Property20_ExponentialBackoffEdgeCases) {
    /* Test with very small initial timeout */
    int32_t backoff = xgl_reliable_calc_backoff(1, 5);
    EXPECT_EQ(backoff, 32);  /* 1 * 2^5 = 32 */

    /* Test with zero initial timeout */
    backoff = xgl_reliable_calc_backoff(0, 5);
    EXPECT_EQ(backoff, 0);

    /* Test with large initial timeout that would overflow */
    backoff = xgl_reliable_calc_backoff(10000, 5);
    EXPECT_LE(backoff, 30000);  /* Should be capped */

    /* Test with maximum retry count */
    backoff = xgl_reliable_calc_backoff(100, 255);
    EXPECT_LE(backoff, 30000);  /* Should be capped */
    EXPECT_GT(backoff, 0);

    /* Test that backoff is capped at 30000 */
    backoff = xgl_reliable_calc_backoff(5000, 10);
    EXPECT_EQ(backoff, 30000);
}

/*---------------------------------------------------------------------------*/
/* Property 16: ACK Processing                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 16: ACK Processing
 * \details         For any received ACK with matching Packet number and
 *                  target ID, the transport layer should remove the
 *                  corresponding packet from the wait-ACK queue.
 * \note            Validates: Requirements 5.4
 */
TEST(XglTransportProperties, Property16_ACKProcessing) {
    PropertyTestGenerator gen;

    /* Test with 100+ random ACK scenarios */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_phy_ops_t phy;
        phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
            (void)data; (void)len; (void)user_data;
            return XGL_OK;
        };
        phy.rx = nullptr;
        phy.user_data = nullptr;

        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, 5, nullptr);
        ASSERT_EQ(err, XGL_OK) << "Queue initialization failed";

        /* Generate random packet parameters */
        uint16_t source_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        uint16_t target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        uint32_t packet_number = gen.random_uint32();
        std::vector<uint8_t> data = gen.random_bytes(10 + (gen.random_uint8() % 50));

        /* Add packet to queue */
        err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                     source_id, target_id, packet_number,
                                     0, 0, 1000, &phy);
        ASSERT_EQ(err, XGL_OK) << "Failed to add packet to queue";

        /* Verify packet is in queue */
        EXPECT_EQ(xgl_reliable_get_count(&queue), 1)
            << "Queue should contain one packet";

        xgl_reliable_packet_t* packet = xgl_reliable_find_packet_number(&queue, packet_number, target_id);
        ASSERT_NE(packet, nullptr)
            << "Packet should be findable in queue";

        /* Process ACK with matching Packet number and target ID */
        xgl_error_t remove_err = xgl_reliable_remove_packet_number(&queue, packet_number, target_id);

        EXPECT_EQ(remove_err, XGL_OK)
            << "ACK processing should remove matching packet from queue"
            << "\n  packet_number: " << (int)packet_number
            << "\n  target_id: " << (int)target_id;

        /* Verify packet was removed from queue */
        EXPECT_EQ(xgl_reliable_get_count(&queue), 0)
            << "Queue should be empty after ACK processing";

        EXPECT_TRUE(xgl_reliable_is_empty(&queue))
            << "Queue should be empty after ACK removes packet";

        /* Verify packet is no longer findable */
        packet = xgl_reliable_find_packet_number(&queue, packet_number, target_id);
        EXPECT_EQ(packet, nullptr)
            << "Packet should not be findable after ACK processing";

        xgl_reliable_destroy(&queue);
    }
}

/**
 * \brief           Test ACK processing with multiple packets
 * \details         Verifies ACK removes only the matching packet
 */
TEST(XglTransportProperties, Property16_ACKProcessingMultiplePackets) {
    PropertyTestGenerator gen;

    xgl_phy_ops_t phy;
    phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
        (void)data; (void)len; (void)user_data;
        return XGL_OK;
    };
    phy.rx = nullptr;
    phy.user_data = nullptr;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, 10, nullptr);
        ASSERT_EQ(err, XGL_OK);

        /* Add multiple packets with different Packet numbers */
        const int num_packets = 5;
        uint16_t target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);

        for (int i = 0; i < num_packets; ++i) {
            std::vector<uint8_t> data = gen.random_bytes(20);
            err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                         1, target_id, (uint8_t)i,
                                         0, 0, 1000, &phy);
            ASSERT_EQ(err, XGL_OK);
        }

        EXPECT_EQ(xgl_reliable_get_count(&queue), (size_t)num_packets);

        /* ACK middle packet (packet_number = 2) */
        xgl_error_t remove_err = xgl_reliable_remove_packet_number(&queue, 2, target_id);
        EXPECT_EQ(remove_err, XGL_OK);

        /* Verify only that packet was removed */
        EXPECT_EQ(xgl_reliable_get_count(&queue), (size_t)(num_packets - 1))
            << "Only ACKed packet should be removed";

        /* Verify other packets still exist */
        EXPECT_NE(xgl_reliable_find_packet_number(&queue, 0, target_id), nullptr);
        EXPECT_NE(xgl_reliable_find_packet_number(&queue, 1, target_id), nullptr);
        EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 2, target_id), nullptr);  /* Removed */
        EXPECT_NE(xgl_reliable_find_packet_number(&queue, 3, target_id), nullptr);
        EXPECT_NE(xgl_reliable_find_packet_number(&queue, 4, target_id), nullptr);

        xgl_reliable_destroy(&queue);
    }
}

/**
 * \brief           Test ACK processing with non-matching Packet number
 * \details         Verifies ACK with wrong Packet number doesn't remove packet
 */
TEST(XglTransportProperties, Property16_ACKProcessingNonMatching) {
    PropertyTestGenerator gen;

    xgl_phy_ops_t phy;
    phy.tx = [](const uint8_t* data, size_t len, void* user_data) -> xgl_error_t {
        (void)data; (void)len; (void)user_data;
        return XGL_OK;
    };
    phy.rx = nullptr;
    phy.user_data = nullptr;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS; ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, 5, nullptr);
        ASSERT_EQ(err, XGL_OK);

        /* Add packet with specific Packet number */
        uint32_t packet_number = gen.random_uint32();
        uint16_t target_id = static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        std::vector<uint8_t> data = gen.random_bytes(20);

        err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                     1, target_id, packet_number, 0, 0, 1000, &phy);
        ASSERT_EQ(err, XGL_OK);

        /* Try to ACK with different Packet number */
        uint32_t wrong_packet_number = packet_number + 1U;
        xgl_error_t remove_err = xgl_reliable_remove_packet_number(&queue, wrong_packet_number, target_id);

        EXPECT_NE(remove_err, XGL_OK)
            << "ACK with non-matching Packet number should not remove packet";

        /* Verify packet still exists */
        EXPECT_EQ(xgl_reliable_get_count(&queue), 1)
            << "Packet should remain in queue";

        EXPECT_NE(xgl_reliable_find_packet_number(&queue, packet_number, target_id), nullptr)
            << "Original packet should still be findable";

        xgl_reliable_destroy(&queue);
    }
}


