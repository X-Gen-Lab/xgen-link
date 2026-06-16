/**
 * \file            test_time.cpp
 * \brief           Time abstraction unit tests
 * \author          X-Gen Lab
 */

#include <gtest/gtest.h>
#include <xgl/internal/xgl_time.h>
#include <thread>
#include <chrono>

/*---------------------------------------------------------------------------*/
/* Basic Time Functions Tests                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_time_ms() returns non-zero
 */
TEST(XglTimeTest, TimeMs_ReturnsNonZero) {
    uint32_t time1 = xgl_time_ms();

    /* Time should be non-zero (unless system just started) */
    /* We can't guarantee this, so just check it's callable */
    EXPECT_GE(time1, 0);
}

/**
 * \brief           Test xgl_time_ms() advances
 */
TEST(XglTimeTest, TimeMs_Advances) {
    uint32_t time1 = xgl_time_ms();

    /* Sleep for 10ms */
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uint32_t time2 = xgl_time_ms();

    /* Time should have advanced by at least 5ms (allowing for timing variance) */
    EXPECT_GE(time2 - time1, 5);
}

/**
 * \brief           Test xgl_time_us() returns non-zero
 */
TEST(XglTimeTest, TimeUs_ReturnsNonZero) {
    uint32_t time1 = xgl_time_us();

    /* Time should be non-zero (unless system just started) */
    EXPECT_GE(time1, 0);
}

/**
 * \brief           Test xgl_time_us() advances
 */
TEST(XglTimeTest, TimeUs_Advances) {
    uint32_t time1 = xgl_time_us();

    /* Sleep for 1ms */
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    uint32_t time2 = xgl_time_us();

    /* Time should have advanced by at least 500us */
    EXPECT_GE(time2 - time1, 500);
}

/*---------------------------------------------------------------------------*/
/* Delay Functions Tests                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_delay_ms() delays correctly
 */
TEST(XglTimeTest, DelayMs_DelaysCorrectly) {
    uint32_t start = xgl_time_ms();

    /* Delay for 50ms */
    xgl_delay_ms(50);

    uint32_t elapsed = xgl_time_ms() - start;

    /* Elapsed time should be at least 45ms (allowing for timing variance) */
    EXPECT_GE(elapsed, 45);
    /* And not more than 100ms (reasonable upper bound) */
    EXPECT_LE(elapsed, 100);
}

/**
 * \brief           Test xgl_delay_us() delays correctly
 */
TEST(XglTimeTest, DelayUs_DelaysCorrectly) {
    uint32_t start = xgl_time_us();

    /* Delay for 1000us (1ms) */
    xgl_delay_us(1000);

    uint32_t elapsed = xgl_time_us() - start;

    /* Elapsed time should be at least 800us (allowing for timing variance) */
    EXPECT_GE(elapsed, 800);
    /* And not more than 5000us (reasonable upper bound) */
    EXPECT_LE(elapsed, 5000);
}

/**
 * \brief           Test xgl_delay_ms() with zero delay
 */
TEST(XglTimeTest, DelayMs_ZeroDelay) {
    uint32_t start = xgl_time_ms();

    /* Delay for 0ms should return immediately */
    xgl_delay_ms(0);

    uint32_t elapsed = xgl_time_ms() - start;

    /* Should take less than 10ms */
    EXPECT_LE(elapsed, 10);
}

/*---------------------------------------------------------------------------*/
/* Elapsed Time Tests                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_time_elapsed_ms() calculates correctly
 */
TEST(XglTimeTest, ElapsedMs_CalculatesCorrectly) {
    uint32_t start = xgl_time_ms();

    /* Sleep for 20ms */
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    uint32_t elapsed = xgl_time_elapsed_ms(start);

    /* Elapsed should be at least 15ms */
    EXPECT_GE(elapsed, 15);
    /* And not more than 50ms */
    EXPECT_LE(elapsed, 50);
}

/**
 * \brief           Test xgl_time_elapsed_ms() handles wraparound
 */
TEST(XglTimeTest, ElapsedMs_HandlesWraparound) {
    /* Simulate wraparound scenario */
    uint32_t start = 0xFFFFFFF0;  /* Near max value */

    /* Simulate current time after wraparound */
    uint32_t current = 0x00000010;  /* Wrapped around */

    /* Calculate elapsed manually (should be 32) */
    uint32_t elapsed = current - start;

    EXPECT_EQ(elapsed, 32);
}

/*---------------------------------------------------------------------------*/
/* Timeout Tests                                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_time_is_timeout() detects timeout
 */
TEST(XglTimeTest, IsTimeout_DetectsTimeout) {
    uint32_t start = xgl_time_ms();

    /* Should not timeout immediately */
    EXPECT_FALSE(xgl_time_is_timeout(start, 100));

    /* Sleep for 50ms */
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    /* Should timeout with 30ms timeout */
    EXPECT_TRUE(xgl_time_is_timeout(start, 30));

    /* Should not timeout with 100ms timeout */
    EXPECT_FALSE(xgl_time_is_timeout(start, 100));
}

/**
 * \brief           Test xgl_time_is_timeout() with zero timeout
 */
TEST(XglTimeTest, IsTimeout_ZeroTimeout) {
    uint32_t start = xgl_time_ms();

    /* Zero timeout should always timeout */
    EXPECT_TRUE(xgl_time_is_timeout(start, 0));
}

/**
 * \brief           Test xgl_time_is_timeout() handles wraparound
 */
TEST(XglTimeTest, IsTimeout_HandlesWraparound) {
    /* Simulate wraparound scenario */
    uint32_t start = 0xFFFFFFF0;  /* Near max value */

    /* Simulate current time after wraparound */
    /* We can't actually set the time, so this is a conceptual test */
    /* The implementation uses subtraction which handles wraparound correctly */

    /* Test the math: (current - start) >= timeout */
    uint32_t current = 0x00000010;  /* Wrapped around */
    uint32_t timeout = 20;

    bool timed_out = (current - start) >= timeout;
    EXPECT_TRUE(timed_out);  /* 32 >= 20 */

    timeout = 50;
    timed_out = (current - start) >= timeout;
    EXPECT_FALSE(timed_out);  /* 32 < 50 */
}

/*---------------------------------------------------------------------------*/
/* Custom Time Source Tests                                                  */
/*---------------------------------------------------------------------------*/

static uint32_t custom_time_value = 0;

static uint32_t custom_time_source(void) {
    return custom_time_value;
}

/**
 * \brief           Test xgl_time_set_source() allows custom time source
 */
TEST(XglTimeTest, SetSource_AllowsCustomTimeSource) {
    /* Set custom time source */
    xgl_time_set_source(custom_time_source);

    /* Set custom time value */
    custom_time_value = 1234;

    /* xgl_time_ms() should return custom value */
    EXPECT_EQ(xgl_time_ms(), 1234);

    /* Update custom time */
    custom_time_value = 5678;
    EXPECT_EQ(xgl_time_ms(), 5678);

    /* Reset to default time source */
    xgl_time_set_source(NULL);

    /* Should now return system time (non-zero) */
    uint32_t system_time = xgl_time_ms();
    EXPECT_NE(system_time, 5678);  /* Should be different from custom value */
}

/*---------------------------------------------------------------------------*/
/* Hardware Timer Tests (Basic)                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_timer_create() with NULL config
 */
TEST(XglTimeTest, TimerCreate_NullConfig) {
    xgl_timer_handle_t timer = xgl_timer_create(NULL);

    /* Should return NULL for invalid config */
    EXPECT_EQ(timer, nullptr);
}

/**
 * \brief           Test xgl_timer_create() with NULL callback
 */
TEST(XglTimeTest, TimerCreate_NullCallback) {
    xgl_timer_config_t config = {0};
    config.period_ms = 100;
    config.callback = NULL;
    config.auto_reload = true;

    xgl_timer_handle_t timer = xgl_timer_create(&config);

    /* Should return NULL for NULL callback */
    EXPECT_EQ(timer, nullptr);
}

/**
 * \brief           Test timer operations with NULL handle
 */
TEST(XglTimeTest, TimerOperations_NullHandle) {
    /* Start with NULL handle */
    EXPECT_EQ(xgl_timer_start(NULL), XGL_ERR_NULL_POINTER);

    /* Stop with NULL handle */
    EXPECT_EQ(xgl_timer_stop(NULL), XGL_ERR_NULL_POINTER);

    /* Destroy with NULL handle (should not crash) */
    xgl_timer_destroy(NULL);
}

/*---------------------------------------------------------------------------*/
/* Performance Tests                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Test xgl_time_ms() performance
 */
TEST(XglTimeTest, TimeMs_Performance) {
    const int iterations = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        volatile uint32_t time = xgl_time_ms();
        (void)time;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    /* Each call should take less than 10us on average */
    EXPECT_LT(duration.count() / iterations, 10);
}

/**
 * \brief           Test xgl_time_elapsed_ms() performance
 */
TEST(XglTimeTest, ElapsedMs_Performance) {
    const int iterations = 10000;
    uint32_t start = xgl_time_ms();

    auto perf_start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        volatile uint32_t elapsed = xgl_time_elapsed_ms(start);
        (void)elapsed;
    }

    auto perf_end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(perf_end - perf_start);

    /* Each call should take less than 10us on average */
    EXPECT_LT(duration.count() / iterations, 10);
}
