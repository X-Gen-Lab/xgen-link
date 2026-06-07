/**
 * \file            xgl_time_provider.c
 * \brief           Time provider implementation
 * \author          Nexus Team
 */

#include "xgl/xgl_time_provider.h"
#include "xgl/xgl_time.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* Default Time Provider                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default time provider function (uses xgl_time_ms)
 */
static uint32_t default_time_provider_fn(void* user_data) {
    (void)user_data;  /* Unused */
    return xgl_time_ms();
}

/**
 * \brief           Get default system time provider
 */
xgl_time_provider_t xgl_time_provider_default(void) {
    xgl_time_provider_t provider;
    provider.get_time_ms = default_time_provider_fn;
    provider.user_data = NULL;
    return provider;
}

/*---------------------------------------------------------------------------*/
/* Mock Time Provider                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Mock time provider function
 */
// cppcheck-suppress constParameterCallback
static uint32_t mock_time_provider_fn(void* user_data) {
    const xgl_mock_time_t* mock = (const xgl_mock_time_t*)user_data;
    if (mock == NULL) {
        return 0;
    }
    return mock->current_time_ms;
}

/**
 * \brief           Initialize mock time provider
 */
void xgl_mock_time_init(xgl_mock_time_t* mock, uint32_t initial_time_ms) {
    if (mock == NULL) {
        return;
    }
    mock->current_time_ms = initial_time_ms;
}

/**
 * \brief           Advance mock time
 */
void xgl_mock_time_advance(xgl_mock_time_t* mock, uint32_t delta_ms) {
    if (mock == NULL) {
        return;
    }
    mock->current_time_ms += delta_ms;
}

/**
 * \brief           Set mock time to specific value
 */
void xgl_mock_time_set(xgl_mock_time_t* mock, uint32_t time_ms) {
    if (mock == NULL) {
        return;
    }
    mock->current_time_ms = time_ms;
}

/**
 * \brief           Create time provider from mock time
 */
xgl_time_provider_t xgl_time_provider_mock(xgl_mock_time_t* mock) {
    xgl_time_provider_t provider;
    provider.get_time_ms = mock_time_provider_fn;
    provider.user_data = mock;
    return provider;
}
