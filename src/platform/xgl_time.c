/**
 * \file            xgl_time.c
 * \brief           Time abstraction implementation
 * \author          Nexus Team
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "xgl/internal/xgl_time.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* Custom Time Source (Optional)                                             */
/*---------------------------------------------------------------------------*/

/* Function pointer for custom time source */
static uint32_t (*custom_time_source)(void) = NULL;

/**
 * \brief           Set custom time source
 */
void xgl_time_set_source(uint32_t (*time_fn)(void)) {
    custom_time_source = time_fn;
}

/*---------------------------------------------------------------------------*/
/* POSIX Implementation (Linux, macOS, Unix)                                */
/*---------------------------------------------------------------------------*/

#if defined(XGL_PLATFORM_POSIX)

#include <time.h>
#include <unistd.h>
#include <sys/time.h>

/**
 * \brief           Get current time in milliseconds (POSIX)
 */
uint32_t xgl_time_ms(void) {
    /* Use custom time source if set */
    if (custom_time_source != NULL) {
        return custom_time_source();
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/**
 * \brief           Get current time in microseconds (POSIX)
 */
uint32_t xgl_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}

/**
 * \brief           Delay for milliseconds (POSIX)
 */
void xgl_delay_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

/**
 * \brief           Delay for microseconds (POSIX)
 */
void xgl_delay_us(uint32_t us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

/*---------------------------------------------------------------------------*/
/* Windows Implementation                                                    */
/*---------------------------------------------------------------------------*/

#elif defined(XGL_PLATFORM_WINDOWS)

#include <windows.h>

/* Static variables for high-resolution timer */
static LARGE_INTEGER frequency;
static LARGE_INTEGER start_time;
static bool timer_initialized = false;

/**
 * \brief           Initialize Windows high-resolution timer
 */
static void xgl_time_init_windows(void) {
    if (!timer_initialized) {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start_time);
        timer_initialized = true;
    }
}

/**
 * \brief           Get current time in milliseconds (Windows)
 */
uint32_t xgl_time_ms(void) {
    /* Use custom time source if set */
    if (custom_time_source != NULL) {
        return custom_time_source();
    }
    
    xgl_time_init_windows();
    
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    
    /* Calculate elapsed time in milliseconds */
    uint64_t elapsed = (uint64_t)(current_time.QuadPart - start_time.QuadPart);
    return (uint32_t)((elapsed * 1000U) / (uint64_t)frequency.QuadPart);
}

/**
 * \brief           Get current time in microseconds (Windows)
 */
uint32_t xgl_time_us(void) {
    xgl_time_init_windows();
    
    LARGE_INTEGER current_time;
    QueryPerformanceCounter(&current_time);
    
    /* Calculate elapsed time in microseconds */
    uint64_t elapsed = (uint64_t)(current_time.QuadPart - start_time.QuadPart);
    return (uint32_t)((elapsed * 1000000U) / (uint64_t)frequency.QuadPart);
}

/**
 * \brief           Delay for milliseconds (Windows)
 */
void xgl_delay_ms(uint32_t ms) {
    Sleep(ms);
}

/**
 * \brief           Delay for microseconds (Windows)
 */
void xgl_delay_us(uint32_t us) {
    /* Windows Sleep() has ~1ms resolution, use busy-wait for microseconds */
    uint32_t start = xgl_time_us();
    while ((xgl_time_us() - start) < us) {
        /* Busy wait */
    }
}

/*---------------------------------------------------------------------------*/
/* FreeRTOS Implementation                                                   */
/*---------------------------------------------------------------------------*/

#elif defined(XGL_PLATFORM_FREERTOS)

#include "FreeRTOS.h"
#include "task.h"

/**
 * \brief           Get current time in milliseconds (FreeRTOS)
 */
uint32_t xgl_time_ms(void) {
    /* Use custom time source if set */
    if (custom_time_source != NULL) {
        return custom_time_source();
    }
    
    /* Convert ticks to milliseconds */
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * \brief           Get current time in microseconds (FreeRTOS)
 */
uint32_t xgl_time_us(void) {
    /* FreeRTOS tick resolution is typically 1ms, so microsecond precision not available */
    /* Return milliseconds * 1000 as approximation */
    return xgl_time_ms() * 1000;
}

/**
 * \brief           Delay for milliseconds (FreeRTOS)
 */
void xgl_delay_ms(uint32_t ms) {
    /* Convert milliseconds to ticks */
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks);
}

/**
 * \brief           Delay for microseconds (FreeRTOS)
 */
void xgl_delay_us(uint32_t us) {
    /* FreeRTOS doesn't support microsecond delays, use milliseconds */
    uint32_t ms = (us + 999) / 1000;  /* Round up */
    if (ms > 0) {
        xgl_delay_ms(ms);
    }
}

/*---------------------------------------------------------------------------*/
/* Bare-Metal Implementation (User must provide)                             */
/*---------------------------------------------------------------------------*/

#else

/* Weak symbols that can be overridden by user implementation */
__attribute__((weak)) uint32_t xgl_time_ms(void) {
    /* Use custom time source if set */
    if (custom_time_source != NULL) {
        return custom_time_source();
    }
    
    /* Default: return 0 (user must provide implementation) */
    return 0;
}

__attribute__((weak)) uint32_t xgl_time_us(void) {
    /* Default: return milliseconds * 1000 */
    return xgl_time_ms() * 1000;
}

__attribute__((weak)) void xgl_delay_ms(uint32_t ms) {
    /* Default: busy-wait using xgl_time_ms() */
    uint32_t start = xgl_time_ms();
    while ((xgl_time_ms() - start) < ms) {
        /* Busy wait */
    }
}

__attribute__((weak)) void xgl_delay_us(uint32_t us) {
    /* Default: busy-wait using xgl_time_us() */
    uint32_t start = xgl_time_us();
    while ((xgl_time_us() - start) < us) {
        /* Busy wait */
    }
}

#endif /* Platform-specific implementations */

/*---------------------------------------------------------------------------*/
/* Hardware Timer Implementation (Platform-Specific)                         */
/*---------------------------------------------------------------------------*/

#if defined(XGL_PLATFORM_FREERTOS)

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
    xgl_timer_freertos_t* timer = (xgl_timer_freertos_t*)pvTimerGetTimerID(xTimer);
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
    
    /* Allocate timer structure */
    xgl_timer_freertos_t* timer = (xgl_timer_freertos_t*)pvPortMalloc(sizeof(xgl_timer_freertos_t));
    if (timer == NULL) {
        return NULL;
    }
    
    timer->callback = config->callback;
    timer->user_data = config->user_data;
    
    /* Create FreeRTOS timer */
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
    
    /* Stop timer if running */
    xTimerStop(timer->handle, 0);
    
    /* Delete timer */
    xTimerDelete(timer->handle, 0);
    
    /* Free memory */
    vPortFree(timer);
}

#else

/**
 * \brief           Create hardware timer (stub for non-FreeRTOS platforms)
 */
xgl_timer_handle_t xgl_timer_create(const xgl_timer_config_t* config) {
    (void)config;
    /* Hardware timers not supported on this platform */
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
