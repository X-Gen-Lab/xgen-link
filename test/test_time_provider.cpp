/**
 * \file            test_time_provider.cpp
 * \brief           Unit tests for time provider abstraction
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_time_provider.h>
#include <xgl/internal/xgl_time.h>

/*---------------------------------------------------------------------------*/
/* Test Fixture                                                              */
/*---------------------------------------------------------------------------*/

class XglTimeProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        /* Initialize mock time */
        xgl_mock_time_init(&mock, 0);
    }

    void TearDown() override {
        /* Nothing to clean up */
    }

    xgl_mock_time_t mock;
};

/*---------------------------------------------------------------------------*/
/* Default Time Provider Tests                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test default time provider creation
 */
TEST_F(XglTimeProviderTest, DefaultProviderCreation) {
    xgl_time_provider_t provider = xgl_time_provider_default();

    EXPECT_NE(provider.get_time_ms, nullptr);
}

/**
 * \brief           Test default time provider returns valid time
 */
TEST_F(XglTimeProviderTest, DefaultProviderReturnsValidTime) {
    xgl_time_provider_t provider = xgl_time_provider_default();

    uint32_t time1 = xgl_time_provider_get_ms(&provider);
    xgl_delay_ms(10);
    uint32_t time2 = xgl_time_provider_get_ms(&provider);

    /* Time should advance */
    EXPECT_GT(time2, time1);
}

/**
 * \brief           Test default time provider is monotonic
 */
TEST_F(XglTimeProviderTest, DefaultProviderIsMonotonic) {
    xgl_time_provider_t provider = xgl_time_provider_default();

    uint32_t prev_time = xgl_time_provider_get_ms(&provider);

    for (int i = 0; i < 10; i++) {
        uint32_t curr_time = xgl_time_provider_get_ms(&provider);
        EXPECT_GE(curr_time, prev_time);
        prev_time = curr_time;
    }
}

/*---------------------------------------------------------------------------*/
/* Mock Time Provider Tests                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test mock time provider creation
 */
TEST_F(XglTimeProviderTest, MockProviderCreation) {
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    EXPECT_NE(provider.get_time_ms, nullptr);
    EXPECT_EQ(provider.user_data, &mock);
}

/**
 * \brief           Test mock time initialization
 */
TEST_F(XglTimeProviderTest, MockTimeInitialization) {
    xgl_mock_time_init(&mock, 1000);

    EXPECT_EQ(mock.current_time_ms, 1000);
}

/**
 * \brief           Test mock time advance
 */
TEST_F(XglTimeProviderTest, MockTimeAdvance) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t time1 = xgl_time_provider_get_ms(&provider);
    EXPECT_EQ(time1, 0);

    xgl_mock_time_advance(&mock, 1000);

    uint32_t time2 = xgl_time_provider_get_ms(&provider);
    EXPECT_EQ(time2, 1000);
}

/**
 * \brief           Test mock time set
 */
TEST_F(XglTimeProviderTest, MockTimeSet) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    xgl_mock_time_set(&mock, 5000);

    uint32_t time = xgl_time_provider_get_ms(&provider);
    EXPECT_EQ(time, 5000);
}

/**
 * \brief           Test mock time multiple advances
 */
TEST_F(XglTimeProviderTest, MockTimeMultipleAdvances) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    xgl_mock_time_advance(&mock, 100);
    EXPECT_EQ(xgl_time_provider_get_ms(&provider), 100);

    xgl_mock_time_advance(&mock, 200);
    EXPECT_EQ(xgl_time_provider_get_ms(&provider), 300);

    xgl_mock_time_advance(&mock, 300);
    EXPECT_EQ(xgl_time_provider_get_ms(&provider), 600);
}

/*---------------------------------------------------------------------------*/
/* Time Provider Utilities Tests                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test elapsed time calculation
 */
TEST_F(XglTimeProviderTest, ElapsedTimeCalculation) {
    xgl_mock_time_init(&mock, 1000);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);
    xgl_mock_time_advance(&mock, 500);

    uint32_t elapsed = xgl_time_provider_elapsed_ms(&provider, start);
    EXPECT_EQ(elapsed, 500);
}

/**
 * \brief           Test timeout detection - not timed out
 */
TEST_F(XglTimeProviderTest, TimeoutDetectionNotTimedOut) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);
    xgl_mock_time_advance(&mock, 500);

    bool timeout = xgl_time_provider_is_timeout(&provider, start, 1000);
    EXPECT_FALSE(timeout);
}

/**
 * \brief           Test timeout detection - timed out
 */
TEST_F(XglTimeProviderTest, TimeoutDetectionTimedOut) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);
    xgl_mock_time_advance(&mock, 1500);

    bool timeout = xgl_time_provider_is_timeout(&provider, start, 1000);
    EXPECT_TRUE(timeout);
}

/**
 * \brief           Test timeout detection - exact timeout
 */
TEST_F(XglTimeProviderTest, TimeoutDetectionExact) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);
    xgl_mock_time_advance(&mock, 1000);

    bool timeout = xgl_time_provider_is_timeout(&provider, start, 1000);
    EXPECT_TRUE(timeout);
}

/*---------------------------------------------------------------------------*/
/* Wraparound Handling Tests                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test time wraparound handling
 */
TEST_F(XglTimeProviderTest, TimeWraparoundHandling) {
    /* Start near wraparound point */
    xgl_mock_time_init(&mock, 0xFFFFFFF0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);
    EXPECT_EQ(start, 0xFFFFFFF0);

    /* Advance past wraparound */
    xgl_mock_time_advance(&mock, 32);

    uint32_t current = xgl_time_provider_get_ms(&provider);
    EXPECT_EQ(current, 0x10);  /* Wrapped around */

    /* Calculate elapsed time (should handle wraparound) */
    uint32_t elapsed = xgl_time_provider_elapsed_ms(&provider, start);
    EXPECT_EQ(elapsed, 32);
}

/**
 * \brief           Test timeout detection with wraparound
 */
TEST_F(XglTimeProviderTest, TimeoutDetectionWithWraparound) {
    /* Start near wraparound point */
    xgl_mock_time_init(&mock, 0xFFFFFFFF - 500);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);

    /* Advance past wraparound */
    xgl_mock_time_advance(&mock, 1000);

    /* Should detect timeout correctly */
    bool timeout = xgl_time_provider_is_timeout(&provider, start, 800);
    EXPECT_TRUE(timeout);
}

/*---------------------------------------------------------------------------*/
/* Deterministic Testing Scenarios                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test retry logic with mock time
 */
TEST_F(XglTimeProviderTest, RetryLogicWithMockTime) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    const uint32_t retry_timeout = 1000;
    const int max_retries = 3;

    uint32_t send_time = xgl_time_provider_get_ms(&provider);
    int retry_count = 0;

    for (int i = 0; i < max_retries; i++) {
        /* Advance time to trigger timeout */
        xgl_mock_time_advance(&mock, retry_timeout);

        if (xgl_time_provider_is_timeout(&provider, send_time, retry_timeout)) {
            retry_count++;
            send_time = xgl_time_provider_get_ms(&provider);
        }
    }

    EXPECT_EQ(retry_count, max_retries);
    EXPECT_EQ(xgl_time_provider_get_ms(&provider), max_retries * retry_timeout);
}

/**
 * \brief           Test RTT measurement with mock time
 */
TEST_F(XglTimeProviderTest, RTTMeasurementWithMockTime) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    const uint32_t expected_rtt = 50;

    uint32_t send_time = xgl_time_provider_get_ms(&provider);

    /* Simulate network delay */
    xgl_mock_time_advance(&mock, expected_rtt);

    uint32_t recv_time = xgl_time_provider_get_ms(&provider);
    uint32_t measured_rtt = recv_time - send_time;

    EXPECT_EQ(measured_rtt, expected_rtt);
}

/**
 * \brief           Test exponential backoff with mock time
 */
TEST_F(XglTimeProviderTest, ExponentialBackoffWithMockTime) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t base_timeout = 100;
    uint32_t send_time = xgl_time_provider_get_ms(&provider);

    for (int retry = 0; retry < 4; retry++) {
        uint32_t timeout = base_timeout * (1 << retry);  /* Exponential backoff */

        xgl_mock_time_advance(&mock, timeout);

        bool timed_out = xgl_time_provider_is_timeout(&provider, send_time, timeout);
        EXPECT_TRUE(timed_out);

        send_time = xgl_time_provider_get_ms(&provider);
    }
}

/*---------------------------------------------------------------------------*/
/* Edge Cases Tests                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test zero timeout
 */
TEST_F(XglTimeProviderTest, ZeroTimeout) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);

    /* Zero timeout should immediately timeout */
    bool timeout = xgl_time_provider_is_timeout(&provider, start, 0);
    EXPECT_TRUE(timeout);
}

/**
 * \brief           Test maximum timeout value
 */
TEST_F(XglTimeProviderTest, MaximumTimeout) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);

    /* Advance by half of max uint32 */
    xgl_mock_time_advance(&mock, 0x7FFFFFFF);

    /* Should not timeout with max timeout */
    bool timeout = xgl_time_provider_is_timeout(&provider, start, UINT32_MAX);
    EXPECT_FALSE(timeout);
}

/**
 * \brief           Test NULL provider handling
 */
TEST_F(XglTimeProviderTest, NullProviderHandling) {
    /* Getting time from NULL provider should return 0 */
    uint32_t time = xgl_time_provider_get_ms(nullptr);
    EXPECT_EQ(time, 0);
}

/*---------------------------------------------------------------------------*/
/* Performance Tests                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test mock time provider performance
 */
TEST_F(XglTimeProviderTest, MockTimeProviderPerformance) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    /* Get time many times - should be fast */
    for (int i = 0; i < 1000; i++) {
        uint32_t time = xgl_time_provider_get_ms(&provider);
        (void)time;  /* Suppress unused warning */
    }

    /* Test passed if it completes quickly */
    SUCCEED();
}

/**
 * \brief           Test time provider utilities performance
 */
TEST_F(XglTimeProviderTest, TimeProviderUtilitiesPerformance) {
    xgl_mock_time_init(&mock, 0);
    xgl_time_provider_t provider = xgl_time_provider_mock(&mock);

    uint32_t start = xgl_time_provider_get_ms(&provider);

    /* Call utilities many times */
    for (int i = 0; i < 1000; i++) {
        xgl_time_provider_elapsed_ms(&provider, start);
        xgl_time_provider_is_timeout(&provider, start, 1000);
    }

    /* Test passed if it completes quickly */
    SUCCEED();
}

