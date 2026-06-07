/**
 * \file            xgl_time.h
 * \brief           Time abstraction layer
 * \author          Nexus Team
 */

#ifndef XGL_TIME_H
#define XGL_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Platform Detection                                                        */
/*---------------------------------------------------------------------------*/

/* Detect platform for time implementation */
#if defined(_WIN32) || defined(_WIN64)
    #define XGL_PLATFORM_WINDOWS
#elif defined(__unix__) || defined(__unix) || defined(__linux__) || defined(__APPLE__)
    #define XGL_PLATFORM_POSIX
#elif defined(__FREERTOS__)
    #define XGL_PLATFORM_FREERTOS
#else
    #define XGL_PLATFORM_BAREMETAL
#endif

/*---------------------------------------------------------------------------*/
/* Hardware Timer Configuration                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Hardware timer callback function type
 * \param[in]       user_data: User data passed to callback
 * \note            Called from timer interrupt context
 */
typedef void (*xgl_timer_callback_t)(void* user_data);

/**
 * \brief           Hardware timer configuration
 */
typedef struct {
    uint32_t period_ms;             /**< Timer period in milliseconds */
    xgl_timer_callback_t callback;  /**< Timer callback function */
    void* user_data;                /**< User data for callback */
    bool auto_reload;               /**< Auto-reload timer */
} xgl_timer_config_t;

/**
 * \brief           Hardware timer handle (opaque)
 */
typedef void* xgl_timer_handle_t;

/*---------------------------------------------------------------------------*/
/* Time Functions                                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get current time in milliseconds
 * \return          Current time in milliseconds since system start
 * \note            Wraps around after ~49 days (32-bit counter)
 */
uint32_t xgl_time_ms(void);

/**
 * \brief           Get current time in microseconds
 * \return          Current time in microseconds since system start
 * \note            Wraps around after ~71 minutes (32-bit counter)
 * \note            May not be available on all platforms
 */
uint32_t xgl_time_us(void);

/**
 * \brief           Delay for specified milliseconds
 * \param[in]       ms: Delay time in milliseconds
 * \note            Blocking delay, uses busy-wait or platform sleep
 */
void xgl_delay_ms(uint32_t ms);

/**
 * \brief           Delay for specified microseconds
 * \param[in]       us: Delay time in microseconds
 * \note            Blocking delay, uses busy-wait
 * \note            May not be accurate on all platforms
 */
void xgl_delay_us(uint32_t us);

/**
 * \brief           Calculate elapsed time in milliseconds
 * \param[in]       start_time_ms: Start time from xgl_time_ms()
 * \return          Elapsed time in milliseconds
 * \note            Handles wraparound correctly
 */
static inline uint32_t xgl_time_elapsed_ms(uint32_t start_time_ms) {
    return xgl_time_ms() - start_time_ms;
}

/**
 * \brief           Check if timeout has occurred
 * \param[in]       start_time_ms: Start time from xgl_time_ms()
 * \param[in]       timeout_ms: Timeout duration in milliseconds
 * \return          true if timeout occurred, false otherwise
 * \note            Handles wraparound correctly
 */
static inline bool xgl_time_is_timeout(uint32_t start_time_ms, uint32_t timeout_ms) {
    return (xgl_time_ms() - start_time_ms) >= timeout_ms;
}

/*---------------------------------------------------------------------------*/
/* Hardware Timer Functions (Optional)                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Create hardware timer
 * \param[in]       config: Timer configuration
 * \return          Timer handle on success, NULL on failure
 * \note            Not available on all platforms
 */
xgl_timer_handle_t xgl_timer_create(const xgl_timer_config_t* config);

/**
 * \brief           Start hardware timer
 * \param[in]       handle: Timer handle
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_timer_start(const xgl_timer_handle_t handle);

/**
 * \brief           Stop hardware timer
 * \param[in]       handle: Timer handle
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_timer_stop(const xgl_timer_handle_t handle);

/**
 * \brief           Destroy hardware timer
 * \param[in]       handle: Timer handle
 */
void xgl_timer_destroy(xgl_timer_handle_t handle);

/**
 * \brief           Set custom time source (for testing or custom hardware)
 * \param[in]       time_fn: Function that returns current time in ms
 * \note            Used to override default time source
 */
void xgl_time_set_source(uint32_t (*time_fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* XGL_TIME_H */
