/**
 * \file            xgl_mutex_noop.c
 * \brief           No-op mutex implementation
 * \author          X-Gen Lab
 */

#include "xgl/internal/xgl_mutex.h"

#if !defined(XGL_THREAD_SAFE) || \
    (defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_BAREMETAL))

/**
 * \brief           Initialize no-op mutex
 */
xgl_error_t xgl_mutex_init(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    (void)mutex;
    return XGL_OK;
}

/**
 * \brief           Lock no-op mutex
 */
xgl_error_t xgl_mutex_lock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    (void)mutex;
    return XGL_OK;
}

/**
 * \brief           Try to lock no-op mutex
 */
xgl_error_t xgl_mutex_trylock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    (void)mutex;
    return XGL_OK;
}

/**
 * \brief           Unlock no-op mutex
 */
xgl_error_t xgl_mutex_unlock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    (void)mutex;
    return XGL_OK;
}

/**
 * \brief           Destroy no-op mutex
 */
void xgl_mutex_destroy(xgl_mutex_t* mutex) {
    (void)mutex;
}

#endif /* !XGL_THREAD_SAFE || XGL_PLATFORM_BAREMETAL */
