/**
 * \file            xgl_mutex.h
 * \brief           Thread safety abstraction layer
 * \author          Nexus Team
 */

#ifndef XGL_MUTEX_H
#define XGL_MUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "xgl/xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Platform Detection                                                        */
/*---------------------------------------------------------------------------*/

/* Detect platform for mutex implementation */
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
/* Mutex Type Definition                                                     */
/*---------------------------------------------------------------------------*/

#ifdef XGL_THREAD_SAFE

#if defined(XGL_PLATFORM_WINDOWS)
    /* Windows mutex implementation */
    #include <windows.h>
    typedef struct {
        CRITICAL_SECTION cs;        /**< Windows critical section */
        bool initialized;           /**< Initialization flag */
    } xgl_mutex_t;

#elif defined(XGL_PLATFORM_POSIX)
    /* POSIX mutex implementation */
    #include <pthread.h>
    typedef struct {
        pthread_mutex_t mutex;      /**< POSIX mutex */
        bool initialized;           /**< Initialization flag */
    } xgl_mutex_t;

#elif defined(XGL_PLATFORM_FREERTOS)
    /* FreeRTOS mutex implementation */
    #include "FreeRTOS.h"
    #include "semphr.h"
    typedef struct {
        SemaphoreHandle_t handle;   /**< FreeRTOS semaphore handle */
        StaticSemaphore_t buffer;   /**< Static semaphore buffer */
        bool initialized;           /**< Initialization flag */
    } xgl_mutex_t;

#else
    /* Bare-metal (no-op) implementation */
    typedef struct {
        int dummy;                  /**< Dummy field for valid struct */
    } xgl_mutex_t;
#endif

#else
    /* Thread safety disabled - no-op implementation */
    typedef struct {
        int dummy;                  /**< Dummy field for valid struct */
    } xgl_mutex_t;
#endif /* XGL_THREAD_SAFE */

/*---------------------------------------------------------------------------*/
/* Mutex Functions                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize mutex
 * \param[in,out]   mutex: Pointer to mutex structure
 * \return          XGL_OK on success, error code otherwise
 * \note            Must be called before using the mutex
 */
xgl_error_t xgl_mutex_init(xgl_mutex_t* mutex);

/**
 * \brief           Lock mutex (blocking)
 * \param[in,out]   mutex: Pointer to mutex structure
 * \return          XGL_OK on success, error code otherwise
 * \note            Blocks until mutex is acquired
 */
xgl_error_t xgl_mutex_lock(xgl_mutex_t* mutex);

/**
 * \brief           Try to lock mutex (non-blocking)
 * \param[in,out]   mutex: Pointer to mutex structure
 * \return          XGL_OK if locked, XGL_ERR_BUSY if already locked
 * \note            Returns immediately if mutex is already locked
 */
xgl_error_t xgl_mutex_trylock(xgl_mutex_t* mutex);

/**
 * \brief           Unlock mutex
 * \param[in,out]   mutex: Pointer to mutex structure
 * \return          XGL_OK on success, error code otherwise
 * \note            Must be called by the thread that locked the mutex
 */
xgl_error_t xgl_mutex_unlock(xgl_mutex_t* mutex);

/**
 * \brief           Destroy mutex and release resources
 * \param[in,out]   mutex: Pointer to mutex structure
 * \note            Mutex must not be locked when destroyed
 */
void xgl_mutex_destroy(xgl_mutex_t* mutex);

/*---------------------------------------------------------------------------*/
/* Mutex Lock Guard (RAII-style for C)                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Mutex lock guard structure
 * \note            Used for automatic unlock on scope exit
 */
typedef struct {
    xgl_mutex_t* mutex;             /**< Pointer to mutex */
    bool locked;                    /**< Lock status */
} xgl_mutex_guard_t;

/**
 * \brief           Create mutex lock guard (locks mutex)
 * \param[in,out]   mutex: Pointer to mutex structure
 * \return          Lock guard structure
 * \note            Use with xgl_mutex_guard_unlock() to ensure unlock
 */
xgl_mutex_guard_t xgl_mutex_guard_lock(xgl_mutex_t* mutex);

/**
 * \brief           Release mutex lock guard (unlocks mutex)
 * \param[in,out]   guard: Pointer to lock guard structure
 */
void xgl_mutex_guard_unlock(xgl_mutex_guard_t* guard);

/*---------------------------------------------------------------------------*/
/* Convenience Macros                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Scoped mutex lock (automatic unlock on scope exit)
 * \param[in]       mutex_ptr: Pointer to mutex
 * \note            Uses cleanup attribute (GCC/Clang only)
 */
#if defined(__GNUC__) || defined(__clang__)
    #define XGL_MUTEX_SCOPED_LOCK(mutex_ptr) \
        __attribute__((cleanup(xgl_mutex_guard_unlock))) \
        xgl_mutex_guard_t _xgl_guard_##__LINE__ = xgl_mutex_guard_lock(mutex_ptr)
#else
    /* Fallback for compilers without cleanup attribute */
    #define XGL_MUTEX_SCOPED_LOCK(mutex_ptr) \
        xgl_mutex_guard_t _xgl_guard_##__LINE__ = xgl_mutex_guard_lock(mutex_ptr); \
        /* Manual unlock required */
#endif

#ifdef __cplusplus
}
#endif

#endif /* XGL_MUTEX_H */

