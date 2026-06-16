/**
 * \file            xgl_mutex_windows.c
 * \brief           Windows mutex implementation
 * \author          X-Gen Lab
 */

#include "xgl/internal/xgl_mutex.h"

#if defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)

/**
 * \brief           Initialize Windows mutex
 */
xgl_error_t xgl_mutex_init(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    InitializeCriticalSection(&mutex->cs);
    mutex->initialized = true;

    return XGL_OK;
}

/**
 * \brief           Lock Windows mutex
 */
xgl_error_t xgl_mutex_lock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    EnterCriticalSection(&mutex->cs);
    return XGL_OK;
}

/**
 * \brief           Try to lock Windows mutex
 */
xgl_error_t xgl_mutex_trylock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    BOOL result = TryEnterCriticalSection(&mutex->cs);
    return result ? XGL_OK : XGL_ERR_BUSY;
}

/**
 * \brief           Unlock Windows mutex
 */
xgl_error_t xgl_mutex_unlock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    LeaveCriticalSection(&mutex->cs);
    return XGL_OK;
}

/**
 * \brief           Destroy Windows mutex
 */
void xgl_mutex_destroy(xgl_mutex_t* mutex) {
    if (mutex == NULL || !mutex->initialized) {
        return;
    }

    DeleteCriticalSection(&mutex->cs);
    mutex->initialized = false;
}

#endif /* XGL_THREAD_SAFE && XGL_PLATFORM_WINDOWS */
