/**
 * \file            xgl_time_provider.h
 * \brief           Time provider abstraction for testability
 * \author          X-Gen Lab
 * \details         Provides an injectable time source interface to enable
 *                  deterministic testing and simulation of time-dependent
 *                  protocol behavior
 */

#ifndef XGL_TIME_PROVIDER_H
#define XGL_TIME_PROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------*/
/* Time Provider Interface                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Time provider function type
 * \param[in]       user_data: Optional user data pointer
 * \return          Current time in milliseconds
 * \note            Must be monotonic and handle wraparound correctly
 */
typedef uint32_t (*xgl_time_provider_fn)(void* user_data);

/**
 * \brief           Time provider structure
 * \details         Encapsulates time source for dependency injection
 */
typedef struct {
    xgl_time_provider_fn get_time_ms;  /**< Get current time in milliseconds */
    void* user_data;                    /**< Optional user data for provider */
} xgl_time_provider_t;

/*---------------------------------------------------------------------------*/
/* Default Time Provider                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get default system time provider
 * \return          Time provider using xgl_time_ms()
 * \note            Uses platform-specific time source (POSIX, Windows, etc.)
 */
xgl_time_provider_t xgl_time_provider_default(void);

/*---------------------------------------------------------------------------*/
/* Mock Time Provider (for testing)                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Mock time provider state
 * \details         Allows manual control of time for deterministic testing
 */
typedef struct {
    uint32_t current_time_ms;       /**< Current simulated time */
} xgl_mock_time_t;

/**
 * \brief           Initialize mock time provider
 * \param[in]       mock: Mock time state structure
 * \param[in]       initial_time_ms: Initial time value
 */
void xgl_mock_time_init(xgl_mock_time_t* mock, uint32_t initial_time_ms);

/**
 * \brief           Advance mock time
 * \param[in]       mock: Mock time state structure
 * \param[in]       delta_ms: Time to advance in milliseconds
 */
void xgl_mock_time_advance(xgl_mock_time_t* mock, uint32_t delta_ms);

/**
 * \brief           Set mock time to specific value
 * \param[in]       mock: Mock time state structure
 * \param[in]       time_ms: New time value
 */
void xgl_mock_time_set(xgl_mock_time_t* mock, uint32_t time_ms);

/**
 * \brief           Create time provider from mock time
 * \param[in]       mock: Mock time state structure
 * \return          Time provider using mock time source
 */
xgl_time_provider_t xgl_time_provider_mock(xgl_mock_time_t* mock);

/*---------------------------------------------------------------------------*/
/* Time Provider Utilities                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get current time from provider
 * \param[in]       provider: Time provider
 * \return          Current time in milliseconds
 */
static inline uint32_t xgl_time_provider_get_ms(const xgl_time_provider_t* provider) {
    if (provider && provider->get_time_ms) {
        return provider->get_time_ms(provider->user_data);
    }
    return 0;
}

/**
 * \brief           Calculate elapsed time using provider
 * \param[in]       provider: Time provider
 * \param[in]       start_time: Start time from provider
 * \return          Elapsed time in milliseconds
 */
static inline uint32_t xgl_time_provider_elapsed_ms(const xgl_time_provider_t* provider,
                                                     uint32_t start_time) {
    return xgl_time_provider_get_ms(provider) - start_time;
}

/**
 * \brief           Check if timeout occurred using provider
 * \param[in]       provider: Time provider
 * \param[in]       start_time: Start time from provider
 * \param[in]       timeout_ms: Timeout duration
 * \return          true if timeout occurred, false otherwise
 */
static inline bool xgl_time_provider_is_timeout(const xgl_time_provider_t* provider,
                                                uint32_t start_time,
                                                uint32_t timeout_ms) {
    return xgl_time_provider_elapsed_ms(provider, start_time) >= timeout_ms;
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_TIME_PROVIDER_H */
