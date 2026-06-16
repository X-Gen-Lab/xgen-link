/**
 * \file            xgl_timer.c
 * \brief           Hardware timer abstraction implementation
 * \author          X-Gen Lab
 */

#include "xgl/internal/xgl_time.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* Hardware Timer Implementation (Platform-Specific)                         */
/*---------------------------------------------------------------------------*/

#if defined(XGL_PLATFORM_FREERTOS)

#include "FreeRTOS.h"
#include "timers.h"

/**
 * \brief           FreeRTOS timer structure
 */
typedef struct {
    TimerHandle_t handle;           /**< FreeRTOS timer handle */
    StaticTimer_t buffer;           /**< Static timer buffer */
    xgl_timer_callback_t callback;  /**< User callback */
    void* user_data;                /**< User data */
} xgl_timer_freertos_t;

/**
 * \brief           FreeRTOS timer callback wrapper
 */
static void xgl_timer_freertos_callback(TimerHandle_t xTimer) {
    xgl_timer_freertos_t* timer =
        (xgl_timer_freertos_t*)pvTimerGetTimerID(xTimer);
    if (timer != NULL && timer->callback != NULL) {
        timer->callback(timer->user_data);
    }
}

/**
 * \brief           Create hardware timer (FreeRTOS)
 */
xgl_timer_handle_t xgl_timer_create(const xgl_timer_config_t* config) {
    if (config == NULL || config->callback == NULL) {
        return NULL;
    }

    xgl_timer_freertos_t* timer =
        (xgl_timer_freertos_t*)pvPortMalloc(sizeof(xgl_timer_freertos_t));
    if (timer == NULL) {
        return NULL;
    }

    timer->callback = config->callback;
    timer->user_data = config->user_data;

    TickType_t period = pdMS_TO_TICKS(config->period_ms);
    UBaseType_t auto_reload = config->auto_reload ? pdTRUE : pdFALSE;

    timer->handle = xTimerCreateStatic(
        "xgl_timer",
        period,
        auto_reload,
        (void*)timer,
        xgl_timer_freertos_callback,
        &timer->buffer
    );

    if (timer->handle == NULL) {
        vPortFree(timer);
        return NULL;
    }

    return (xgl_timer_handle_t)timer;
}

/**
 * \brief           Start hardware timer (FreeRTOS)
 */
xgl_error_t xgl_timer_start(const xgl_timer_handle_t handle) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_timer_freertos_t* timer = (xgl_timer_freertos_t*)handle;
    BaseType_t result = xTimerStart(timer->handle, 0);

    return (result == pdPASS) ? XGL_OK : XGL_ERR_BUSY;
}

/**
 * \brief           Stop hardware timer (FreeRTOS)
 */
xgl_error_t xgl_timer_stop(const xgl_timer_handle_t handle) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_timer_freertos_t* timer = (xgl_timer_freertos_t*)handle;
    BaseType_t result = xTimerStop(timer->handle, 0);

    return (result == pdPASS) ? XGL_OK : XGL_ERR_BUSY;
}

/**
 * \brief           Destroy hardware timer (FreeRTOS)
 */
void xgl_timer_destroy(xgl_timer_handle_t handle) {
    if (handle == NULL) {
        return;
    }

    xgl_timer_freertos_t* timer = (xgl_timer_freertos_t*)handle;

    xTimerStop(timer->handle, 0);
    xTimerDelete(timer->handle, 0);
    vPortFree(timer);
}

#else

/**
 * \brief           Create hardware timer (stub for non-FreeRTOS platforms)
 */
xgl_timer_handle_t xgl_timer_create(const xgl_timer_config_t* config) {
    (void)config;
    return NULL;
}

/**
 * \brief           Start hardware timer (stub)
 */
xgl_error_t xgl_timer_start(const xgl_timer_handle_t handle) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return XGL_ERR_NOT_INITIALIZED;
}

/**
 * \brief           Stop hardware timer (stub)
 */
xgl_error_t xgl_timer_stop(const xgl_timer_handle_t handle) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    return XGL_ERR_NOT_INITIALIZED;
}

/**
 * \brief           Destroy hardware timer (stub)
 */
void xgl_timer_destroy(xgl_timer_handle_t handle) {
    (void)handle;
}

#endif /* Hardware timer implementation */
