#include <xgl/internal/xgl_transport.h>

#include <xgen/memory/libc_allocator.h>
/**
 * \file            test_transport_properties.cpp
 * \brief           Transport layer property tests
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_reliable.h>
#include <xgl/internal/xgl_rtt.h>
#include <xgl/internal/xgl_window.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl_types.h>

#include <cmath>
#include <gtest/gtest.h>

#include "property_framework.h"

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
static inline bool within_tolerance(int32_t actual, int32_t expected,
                                    int32_t tolerance) {
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
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
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
        int32_t expected_srtt =
            prev_srtt + (error >> XGL_RTT_ALPHA_SHIFT); /* error/8 */

        int32_t abs_error = abs_int32(error);
        int32_t rttvar_delta = abs_error - prev_rttvar;
        int32_t expected_rttvar =
            prev_rttvar + (rttvar_delta >> XGL_RTT_BETA_SHIFT); /* delta/4 */

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

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
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
                int32_t expected_srtt =
                    prev_srtt + (error >> XGL_RTT_ALPHA_SHIFT);

                int32_t abs_error = abs_int32(error);
                int32_t rttvar_delta = abs_error - prev_rttvar;
                int32_t expected_rttvar =
                    prev_rttvar + (rttvar_delta >> XGL_RTT_BETA_SHIFT);

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
    EXPECT_EQ(xgl_rtt_get_rttvar(&est), 0); /* 1/2 = 0 in integer division */

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
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
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
            << "RTO should equal SRTT + 4 * RTTVAR, clamped to [MIN_RTO, "
               "MAX_RTO]"
            << "\n  SRTT: " << srtt << "\n  RTTVAR: " << rttvar
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

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
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
 * \details         Verifies RTO is clamped to MIN_RTO when calculated value is
 * too small
 */
TEST(XglTransportProperties, Property19_ROClampingMinimum) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);

    /* Use very small RTT that would result in RTO < MIN_RTO */
    xgl_rtt_update(&est, 10); /* SRTT=10, RTTVAR=5, RTO=10+4*5=30 */

    int32_t rto = xgl_rtt_get_rto(&est);

    /* RTO should be clamped to MIN_RTO_MS (100) */
    EXPECT_EQ(rto, XGL_MIN_RTO_MS) << "RTO should be clamped to MIN_RTO_MS "
                                      "when calculated value is too small";
}

/**
 * \brief           Test RTO clamping to maximum value
 * \details         Verifies RTO is clamped to MAX_RTO when calculated value is
 * too large
 */
TEST(XglTransportProperties, Property19_ROClampingMaximum) {
    xgl_rtt_estimator_t est;
    xgl_rtt_init(&est);

    /* Use very large RTT that would result in RTO > MAX_RTO */
    xgl_rtt_update(&est,
                   5000); /* SRTT=5000, RTTVAR=2500, RTO=5000+4*2500=15000 */

    int32_t rto = xgl_rtt_get_rto(&est);

    /* RTO should be clamped to MAX_RTO_MS (5000) */
    EXPECT_EQ(rto, XGL_MAX_RTO_MS) << "RTO should be clamped to MAX_RTO_MS "
                                      "when calculated value is too large";
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
    EXPECT_LT(rttvar, 20) << "RTTVAR should be small with stable RTT";

    /* RTO should be close to SRTT when variation is low */
    int32_t rto = xgl_rtt_get_rto(&est);
    int32_t expected_rto = srtt + (XGL_RTO_K_FACTOR * rttvar);
    if (expected_rto < XGL_MIN_RTO_MS) {
        expected_rto = XGL_MIN_RTO_MS;
    }
    if (expected_rto > XGL_MAX_RTO_MS) {
        expected_rto = XGL_MAX_RTO_MS;
    }

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
        if (expected_rto < XGL_MIN_RTO_MS) {
            expected_rto = XGL_MIN_RTO_MS;
        }
        if (expected_rto > XGL_MAX_RTO_MS) {
            expected_rto = XGL_MAX_RTO_MS;
        }

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
    xgl_rtt_init(NULL);        /* Should not crash */
    xgl_rtt_update(NULL, 100); /* Should not crash */
    xgl_rtt_reset(NULL);       /* Should not crash */

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

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        uint8_t window_size = 1 + (gen.random_uint8() % 128);

        xgl_sliding_window_t window;
        xgl_error_t err = xgl_window_init_with_allocator(&window, window_size,
                                                         xgm_allocator_libc());
        ASSERT_EQ(err, XGL_OK) << "Window initialization failed";

        int num_operations = 10 + (gen.random_uint8() % 50);
        for (int op = 0; op < num_operations; ++op) {
            bool do_send = (gen.random_uint8() % 100) < 70;

            if (do_send && xgl_window_can_send_packet_number(&window)) {
                uint32_t before = xgl_window_get_next_packet_number(&window);
                xgl_window_advance_next_packet_number(&window);
                EXPECT_EQ(xgl_window_get_next_packet_number(&window),
                          before + 1U)
                    << "Packet number should advance monotonically";
            } else if (!do_send && xgl_window_get_usage(&window) > 0U) {
                uint32_t outstanding =
                    window.next_packet_number - window.send_base_packet_number;
                uint32_t offset = gen.random_uint32() % outstanding;
                uint32_t packet_to_ack =
                    window.send_base_packet_number + offset;

                err = xgl_window_mark_ack_packet_number(&window, packet_to_ack);
                EXPECT_EQ(err, XGL_OK)
                    << "Marking ACK should succeed for in-window packet";
                (void)xgl_window_advance_base_packet_number(&window);
            }

            uint32_t usage =
                window.next_packet_number - window.send_base_packet_number;
            EXPECT_LE(usage, window_size)
                << "Window invariant violated after operation " << op
                << "\n  send_base_packet_number: "
                << window.send_base_packet_number
                << "\n  next_packet_number: " << window.next_packet_number
                << "\n  window_size: " << (int)window_size
                << "\n  usage: " << usage;

            EXPECT_EQ(xgl_window_can_send_packet_number(&window),
                      usage < window_size)
                << "can_send should be true iff packet-number usage < "
                   "window_size";
        }

        xgl_window_destroy(&window);
    }
}

TEST(XglTransportProperties, Property23_SlidingWindowMaxSize) {
    xgl_sliding_window_t window;
    xgl_error_t err =
        xgl_window_init_with_allocator(&window, 128, xgm_allocator_libc());
    ASSERT_EQ(err, XGL_OK);

    for (int i = 0; i < 128; ++i) {
        EXPECT_TRUE(xgl_window_can_send_packet_number(&window));
        xgl_window_advance_next_packet_number(&window);
    }

    EXPECT_FALSE(xgl_window_can_send_packet_number(&window));
    EXPECT_EQ(xgl_window_get_usage(&window), 128);

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowDoesNotWrapAtEightBits) {
    xgl_sliding_window_t window;
    uint8_t window_size = 16;
    xgl_error_t err = xgl_window_init_with_allocator(&window, window_size,
                                                     xgm_allocator_libc());
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
    xgl_error_t err = xgl_window_init_with_allocator(&window, window_size,
                                                     xgm_allocator_libc());
    ASSERT_EQ(err, XGL_OK);

    for (int i = 0; i < window_size; ++i) {
        xgl_window_advance_next_packet_number(&window);
    }

    for (uint32_t packet_number = 0; packet_number < window_size;
         ++packet_number) {
        err = xgl_window_mark_ack_packet_number(&window, packet_number);
        EXPECT_EQ(err, XGL_OK);
    }

    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), window_size);
    EXPECT_EQ(xgl_window_get_usage(&window), 0);
    EXPECT_TRUE(xgl_window_can_send_packet_number(&window));

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowOutOfOrderAcks) {
    xgl_sliding_window_t window;
    uint8_t window_size = 8;
    xgl_error_t err = xgl_window_init_with_allocator(&window, window_size,
                                                     xgm_allocator_libc());
    ASSERT_EQ(err, XGL_OK);

    for (int i = 0; i < window_size; ++i) {
        xgl_window_advance_next_packet_number(&window);
    }

    const uint32_t ack_order[] = {2, 4, 6, 0, 1, 3, 5, 7};
    for (uint32_t packet_number : ack_order) {
        err = xgl_window_mark_ack_packet_number(&window, packet_number);
        EXPECT_EQ(err, XGL_OK);
        (void)xgl_window_advance_base_packet_number(&window);

        uint32_t usage =
            window.next_packet_number - window.send_base_packet_number;
        EXPECT_LE(usage, window_size);
    }

    EXPECT_EQ(xgl_window_get_usage(&window), 0);

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowReset) {
    PropertyTestGenerator gen;

    xgl_sliding_window_t window;
    uint8_t window_size = 16;
    xgl_error_t err = xgl_window_init_with_allocator(&window, window_size,
                                                     xgm_allocator_libc());
    ASSERT_EQ(err, XGL_OK);

    for (int i = 0; i < 50; ++i) {
        if (xgl_window_can_send_packet_number(&window)) {
            xgl_window_advance_next_packet_number(&window);
        }

        if ((gen.random_uint8() % 3) == 0 &&
            xgl_window_get_usage(&window) > 0U) {
            uint32_t outstanding =
                window.next_packet_number - window.send_base_packet_number;
            uint32_t packet_number = window.send_base_packet_number +
                                     (gen.random_uint32() % outstanding);
            (void)xgl_window_mark_ack_packet_number(&window, packet_number);
            (void)xgl_window_advance_base_packet_number(&window);
        }
    }

    xgl_window_reset(&window);

    EXPECT_EQ(window.send_base_packet_number, 0U);
    EXPECT_EQ(window.next_packet_number, 0U);
    EXPECT_EQ(xgl_window_get_usage(&window), 0);
    EXPECT_TRUE(xgl_window_can_send_packet_number(&window));

    xgl_window_destroy(&window);
}

TEST(XglTransportProperties, Property23_SlidingWindowMinSize) {
    xgl_sliding_window_t window;
    xgl_error_t err =
        xgl_window_init_with_allocator(&window, 1, xgm_allocator_libc());
    ASSERT_EQ(err, XGL_OK);

    EXPECT_TRUE(xgl_window_can_send_packet_number(&window));
    xgl_window_advance_next_packet_number(&window);
    EXPECT_FALSE(xgl_window_can_send_packet_number(&window));
    EXPECT_EQ(xgl_window_get_usage(&window), 1);

    err = xgl_window_mark_ack_packet_number(&window, 0U);
    EXPECT_EQ(err, XGL_OK);
    EXPECT_EQ(xgl_window_advance_base_packet_number(&window), 1);
    EXPECT_TRUE(xgl_window_can_send_packet_number(&window));
    EXPECT_EQ(xgl_window_get_usage(&window), 0);

    xgl_window_destroy(&window);
}

/*---------------------------------------------------------------------------*/
/* Property 13: Reliable Transmission Queuing                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 13: Reliable Transmission
 * Queuing
 * \details         For any packet sent with reliable transmission enabled,
 *                  the transport layer should add it to the wait-ACK queue.
 * \note            Validates: Requirements 5.1
 */
TEST(XglTransportProperties, Property13_ReliableTransmissionQueuing) {
    PropertyTestGenerator gen;

    /* Mock PHY operations */

    /* Test with 100+ random packet configurations */
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, xgm_allocator_libc());
        ASSERT_EQ(err, XGL_OK) << "Queue initialization failed";

        /* Verify queue starts empty */
        EXPECT_TRUE(xgl_reliable_is_empty(&queue))
            << "Queue should be empty initially";
        EXPECT_EQ(xgl_reliable_get_count(&queue), 0)
            << "Queue count should be 0 initially";

        /* Generate random packet data */
        size_t data_len = 1 + (gen.random_uint8() % 255);
        std::vector<uint8_t> data = gen.random_bytes(data_len);

        uint16_t source_id =
            static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        uint16_t target_id =
            static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        uint32_t packet_number = gen.random_uint32();
        uint8_t data_type = gen.random_uint8();
        uint8_t priority = gen.random_uint8() % 8;
        int32_t timeout_ms = 100 + (gen.random_uint32() % 5000);

        /* Add packet to queue */
        err = xgl_reliable_add_packet_number(
            &queue, data.data(), data_len, source_id, target_id, packet_number,
            data_type, priority, timeout_ms, nullptr);

        EXPECT_EQ(err, XGL_OK) << "Adding packet to queue should succeed";

        /* Verify packet was added to queue */
        EXPECT_FALSE(xgl_reliable_is_empty(&queue))
            << "Queue should not be empty after adding packet";

        EXPECT_EQ(xgl_reliable_get_count(&queue), 1)
            << "Queue count should be 1 after adding one packet";

        /* Verify packet can be found in queue */
        xgl_reliable_packet_t* found =
            xgl_reliable_find_packet_number(&queue, packet_number, target_id);
        ASSERT_NE(found, nullptr) << "Packet should be findable in queue";

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

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, xgm_allocator_libc());
        ASSERT_EQ(err, XGL_OK);

        /* Add random number of packets (1-20) */
        int num_packets = 1 + (gen.random_uint8() % 20);

        for (int i = 0; i < num_packets; ++i) {
            std::vector<uint8_t> data =
                gen.random_bytes(10 + (gen.random_uint8() % 100));

            err = xgl_reliable_add_packet_number(
                &queue, data.data(), data.size(),
                static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U),
                static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U),
                static_cast<uint32_t>(i), gen.random_uint8(),
                gen.random_uint8() % 8, 1000, nullptr);

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
 *                  within the timeout period, the packet should be
 * retransmitted.
 * \note            Validates: Requirements 5.2
 */

/**
 * \brief           Test multiple retransmissions
 * \details         Verifies packet is retransmitted multiple times on repeated
 * timeouts
 */

/*---------------------------------------------------------------------------*/
/* Property 15: Retry Exhaustion Handling                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feature: x-gen-link, Property 15: Retry Exhaustion Handling
 * \details         For any packet that exceeds maximum retry count, the
 * transport layer should invoke the error callback and remove the packet from
 * the queue.
 * \note            Validates: Requirements 5.3
 */

/**
 * \brief           Test retry exhaustion with multiple packets
 * \details         Verifies only exhausted packets are removed
 */

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
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        /* Generate random initial timeout (50ms to 2000ms) */
        int32_t initial_timeout = 50 + (gen.random_uint32() % 1950);

        /* Test backoff calculation for retry counts 0-10 */
        int32_t prev_timeout = initial_timeout;

        for (uint8_t retry = 1; retry <= 10; ++retry) {
            int32_t backoff_timeout =
                xgl_reliable_calc_backoff(initial_timeout, retry);

            /* Verify exponential growth: timeout should double each retry */
            int32_t expected_timeout =
                initial_timeout * (1 << retry); /* 2^retry */

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

/**
 * \brief           Test backoff with edge cases
 * \details         Verifies backoff handles boundary conditions correctly
 */
TEST(XglTransportProperties, Property20_ExponentialBackoffEdgeCases) {
    /* Test with very small initial timeout */
    int32_t backoff = xgl_reliable_calc_backoff(1, 5);
    EXPECT_EQ(backoff, 32); /* 1 * 2^5 = 32 */

    /* Test with zero initial timeout */
    backoff = xgl_reliable_calc_backoff(0, 5);
    EXPECT_EQ(backoff, 0);

    /* Test with large initial timeout that would overflow */
    backoff = xgl_reliable_calc_backoff(10000, 5);
    EXPECT_LE(backoff, 30000); /* Should be capped */

    /* Test with maximum retry count */
    backoff = xgl_reliable_calc_backoff(100, 255);
    EXPECT_LE(backoff, 30000); /* Should be capped */
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
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, xgm_allocator_libc());
        ASSERT_EQ(err, XGL_OK) << "Queue initialization failed";

        /* Generate random packet parameters */
        uint16_t source_id =
            static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        uint16_t target_id =
            static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        uint32_t packet_number = gen.random_uint32();
        std::vector<uint8_t> data =
            gen.random_bytes(10 + (gen.random_uint8() % 50));

        /* Add packet to queue */
        err = xgl_reliable_add_packet_number(
            &queue, data.data(), data.size(), source_id, target_id,
            packet_number, 0, 0, 1000, nullptr);
        ASSERT_EQ(err, XGL_OK) << "Failed to add packet to queue";

        /* Verify packet is in queue */
        EXPECT_EQ(xgl_reliable_get_count(&queue), 1)
            << "Queue should contain one packet";

        xgl_reliable_packet_t* packet =
            xgl_reliable_find_packet_number(&queue, packet_number, target_id);
        ASSERT_NE(packet, nullptr) << "Packet should be findable in queue";

        /* Process ACK with matching Packet number and target ID */
        xgl_error_t remove_err =
            xgl_reliable_remove_packet_number(&queue, packet_number, target_id);

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
        packet =
            xgl_reliable_find_packet_number(&queue, packet_number, target_id);
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

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, xgm_allocator_libc());
        ASSERT_EQ(err, XGL_OK);

        /* Add multiple packets with different Packet numbers */
        const int num_packets = 5;
        uint16_t target_id =
            static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);

        for (int i = 0; i < num_packets; ++i) {
            std::vector<uint8_t> data = gen.random_bytes(20);
            err = xgl_reliable_add_packet_number(
                &queue, data.data(), data.size(), 1, target_id, (uint8_t)i, 0,
                0, 1000, nullptr);
            ASSERT_EQ(err, XGL_OK);
        }

        EXPECT_EQ(xgl_reliable_get_count(&queue), (size_t)num_packets);

        /* ACK middle packet (packet_number = 2) */
        xgl_error_t remove_err =
            xgl_reliable_remove_packet_number(&queue, 2, target_id);
        EXPECT_EQ(remove_err, XGL_OK);

        /* Verify only that packet was removed */
        EXPECT_EQ(xgl_reliable_get_count(&queue), (size_t)(num_packets - 1))
            << "Only ACKed packet should be removed";

        /* Verify other packets still exist */
        EXPECT_NE(xgl_reliable_find_packet_number(&queue, 0, target_id),
                  nullptr);
        EXPECT_NE(xgl_reliable_find_packet_number(&queue, 1, target_id),
                  nullptr);
        EXPECT_EQ(xgl_reliable_find_packet_number(&queue, 2, target_id),
                  nullptr); /* Removed */
        EXPECT_NE(xgl_reliable_find_packet_number(&queue, 3, target_id),
                  nullptr);
        EXPECT_NE(xgl_reliable_find_packet_number(&queue, 4, target_id),
                  nullptr);

        xgl_reliable_destroy(&queue);
    }
}

/**
 * \brief           Test ACK processing with non-matching Packet number
 * \details         Verifies ACK with wrong Packet number doesn't remove packet
 */
TEST(XglTransportProperties, Property16_ACKProcessingNonMatching) {
    PropertyTestGenerator gen;

    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        xgl_reliable_queue_t queue;
        xgl_error_t err = xgl_reliable_init(&queue, xgm_allocator_libc());
        ASSERT_EQ(err, XGL_OK);

        /* Add packet with specific Packet number */
        uint32_t packet_number = gen.random_uint32();
        uint16_t target_id =
            static_cast<uint16_t>((gen.random_uint32() % 0xFFFEU) + 1U);
        std::vector<uint8_t> data = gen.random_bytes(20);

        err = xgl_reliable_add_packet_number(&queue, data.data(), data.size(),
                                             1, target_id, packet_number, 0, 0,
                                             1000, nullptr);
        ASSERT_EQ(err, XGL_OK);

        /* Try to ACK with different Packet number */
        uint32_t wrong_packet_number = packet_number + 1U;
        xgl_error_t remove_err = xgl_reliable_remove_packet_number(
            &queue, wrong_packet_number, target_id);

        EXPECT_NE(remove_err, XGL_OK)
            << "ACK with non-matching Packet number should not remove packet";

        /* Verify packet still exists */
        EXPECT_EQ(xgl_reliable_get_count(&queue), 1)
            << "Packet should remain in queue";

        EXPECT_NE(
            xgl_reliable_find_packet_number(&queue, packet_number, target_id),
            nullptr)
            << "Original packet should still be findable";

        xgl_reliable_destroy(&queue);
    }
}

/**
 * \brief           Exercise retry ownership through the typed production path
 */
struct ProductionRetryProbe {
    xgl_transport_ctx_t ctx = {};
    xgl_packet_interface_t lower = {};
    xgl_layer_stats_t stats = {};
    uint64_t retries = 0;
    size_t sends = 0;
    size_t errors = 0;
    std::vector<uint32_t> numbers;

    static xgl_error_t Send(void* opaque, xgl_handle_t, xgl_packet_t* packet) {
        auto* probe = static_cast<ProductionRetryProbe*>(opaque);
        if (packet->packet_type == XGL_PACKET_TYPE_DATA) {
            probe->sends++;
            probe->numbers.push_back(packet->packet_number);
        }
        return XGL_OK;
    }

    static void Error(xgl_handle_t /* handle */, xgl_error_t, const char*,
                      void* opaque) {
        static_cast<ProductionRetryProbe*>(opaque)->errors++;
    }

    xgl_error_t Init(uint8_t max_retry, uint8_t window = 4) {
        xgl_packet_interface_init(&lower, this, Send, nullptr);
        xgl_transport_config_t config = {};
        config.local_id = 1;
        config.max_retry_count = max_retry;
        config.default_timeout_ms = 100;
        config.window_size = window;
        config.max_frame_size = 128;
        config.lower_layer = &lower;
        config.stats = &stats;
        config.tx_retries = &retries;
        config.error_callback = Error;
        config.callback_user_data = this;
        config.allocator = xgm_allocator_libc();
        config.max_peers = 1;
        config.max_tx_packets = window;
        config.max_rx_buffered_packets = window;
        return xgl_transport_init(&ctx, &config);
    }

    xgl_error_t Admit(uint32_t timeout) {
        static const uint8_t payload[] = {1, 2, 3, 4};
        xgl_tx_data_t tx = {};
        tx.target_id = 2;
        tx.data = payload;
        tx.data_len = sizeof(payload);
        tx.reliable = true;
        tx.timeout_ms = timeout;
        return xgl_transport_send(&ctx, nullptr, &tx);
    }

    ~ProductionRetryProbe() {
        xgl_transport_destroy(&ctx);
    }
};

TEST(XglTransportProperties, Property14_RetransmissionOnTimeout) {
    PropertyTestGenerator gen;
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        ProductionRetryProbe probe;
        ASSERT_EQ(probe.Init(3), XGL_OK);
        const uint32_t start = gen.random_uint32();
        const uint32_t timeout = 1U + gen.random_uint32() % 5000U;
        probe.ctx.current_time_ms = start;
        ASSERT_EQ(probe.Admit(timeout), XGL_OK);
        ASSERT_EQ(xgl_transport_run(&probe.ctx, nullptr, start + timeout - 1U),
                  XGL_OK);
        EXPECT_EQ(probe.sends, 1U);
        ASSERT_EQ(xgl_transport_run(&probe.ctx, nullptr, start + timeout),
                  XGL_OK);
        EXPECT_EQ(probe.sends, 2U);
        EXPECT_EQ(probe.numbers, (std::vector<uint32_t>{0U, 0U}));
        EXPECT_EQ(probe.retries, 1U);
        EXPECT_EQ(probe.ctx.peers->tx_window.next_packet_number, 1U);
    }
}

TEST(XglTransportProperties, Property14_RetransmissionMultiple) {
    ProductionRetryProbe probe;
    ASSERT_EQ(probe.Init(3), XGL_OK);
    for (unsigned i = 0; i < 4U; ++i) {
        ASSERT_EQ(probe.Admit(100U + i * 10U), XGL_OK);
    }
    ASSERT_EQ(xgl_transport_run(&probe.ctx, nullptr, 119U), XGL_OK);
    EXPECT_EQ(probe.retries, 2U);
    EXPECT_EQ(probe.numbers, (std::vector<uint32_t>{0, 1, 2, 3, 0, 1}));
    EXPECT_EQ(xgl_reliable_get_count(&probe.ctx.peers->reliable_queue), 4U);
}

TEST(XglTransportProperties, Property15_RetryExhaustionHandling) {
    PropertyTestGenerator gen;
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        const uint8_t max_retry = gen.random_uint8() % 11U;
        ProductionRetryProbe probe;
        ASSERT_EQ(probe.Init(max_retry), XGL_OK);
        ASSERT_EQ(probe.Admit(100), XGL_OK);
        uint32_t now = 0U;
        for (uint8_t retry = 0; retry < max_retry; ++retry) {
            uint32_t delay = 0;
            ASSERT_TRUE(xgl_transport_next_timeout(&probe.ctx, now, &delay));
            now += delay;
            ASSERT_EQ(xgl_transport_run(&probe.ctx, nullptr, now), XGL_OK);
            ASSERT_EQ(xgl_reliable_get_count(&probe.ctx.peers->reliable_queue),
                      1U);
            EXPECT_EQ(probe.errors, 0U);
        }
        uint32_t delay = 0;
        ASSERT_TRUE(xgl_transport_next_timeout(&probe.ctx, now, &delay));
        ASSERT_EQ(xgl_transport_run(&probe.ctx, nullptr, now + delay), XGL_OK);
        EXPECT_TRUE(probe.ctx.peers->failed);
        EXPECT_TRUE(xgl_reliable_is_empty(&probe.ctx.peers->reliable_queue));
        EXPECT_EQ(probe.errors, 1U);
        EXPECT_EQ(probe.Admit(100), XGL_ERR_ACK_TIMEOUT);
        EXPECT_FALSE(
            xgl_transport_next_timeout(&probe.ctx, now + delay, &delay));
        ASSERT_EQ(xgl_transport_run(&probe.ctx, nullptr, now + 60000U), XGL_OK);
        EXPECT_EQ(probe.errors, 1U);
    }
}

TEST(XglTransportProperties, Property15_RetryExhaustionMultiplePackets) {
    ProductionRetryProbe probe;
    ASSERT_EQ(probe.Init(0), XGL_OK);
    ASSERT_EQ(probe.Admit(100), XGL_OK);
    ASSERT_EQ(probe.Admit(200), XGL_OK);
    ASSERT_EQ(probe.Admit(300), XGL_OK);
    ASSERT_EQ(xgl_transport_run(&probe.ctx, nullptr, 100), XGL_OK);
    EXPECT_TRUE(probe.ctx.peers->failed);
    EXPECT_EQ(xgl_reliable_get_count(&probe.ctx.peers->reliable_queue), 0U);
    EXPECT_EQ(probe.errors, 1U);
    EXPECT_EQ(probe.sends, 3U);
}

TEST(XglTransportProperties, Property20_ExponentialBackoffInQueue) {
    PropertyTestGenerator gen;
    for (int iteration = 0; iteration < XGL_PROPERTY_TEST_ITERATIONS;
         ++iteration) {
        ProductionRetryProbe probe;
        ASSERT_EQ(probe.Init(10), XGL_OK);
        const int32_t initial =
            1 + static_cast<int32_t>(gen.random_uint32() % 1000U);
        ASSERT_EQ(probe.Admit(static_cast<uint32_t>(initial)), XGL_OK);
        uint32_t now = 0;
        for (uint8_t retry = 1; retry <= 10U; ++retry) {
            auto* packet = xgl_reliable_find_packet_number(
                &probe.ctx.peers->reliable_queue, 0, 2);
            ASSERT_NE(packet, nullptr);
            now += static_cast<uint32_t>(packet->timeout_ms);
            ASSERT_EQ(xgl_transport_run(&probe.ctx, nullptr, now), XGL_OK);
            EXPECT_EQ(packet->retry_count, retry);
            EXPECT_EQ(packet->timeout_ms,
                      xgl_reliable_calc_backoff(initial, retry));
            EXPECT_EQ(packet->send_timestamp, now);
            EXPECT_EQ(probe.ctx.peers->tx_window.next_packet_number, 1U);
        }
        EXPECT_EQ(probe.errors, 0U);
    }
}
