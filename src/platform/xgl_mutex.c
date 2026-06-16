/**
 * \file            xgl_mutex.c
 * \brief           Thread safety abstraction implementation
 * \author          X-Gen Lab
 */

#include "xgl/internal/xgl_mutex.h"

/*---------------------------------------------------------------------------*/
/* POSIX Implementation (Linux, macOS, Unix)                                */
/*---------------------------------------------------------------------------*/

#if defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_POSIX)

/**
 * \brief           Initialize POSIX mutex
 */
xgl_error_t xgl_mutex_init(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Initialize mutex with default attributes */
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    int result = pthread_mutex_init(&mutex->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    if (result != 0) {
        return XGL_ERR_NO_MEMORY;
    }

    mutex->initialized = true;
    return XGL_OK;
}

/**
 * \brief           Lock POSIX mutex
 */
xgl_error_t xgl_mutex_lock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    int result = pthread_mutex_lock(&mutex->mutex);
    if (result != 0) {
        return XGL_ERR_BUSY;
    }

    return XGL_OK;
}

/**
 * \brief           Try to lock POSIX mutex
 */
xgl_error_t xgl_mutex_trylock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    int result = pthread_mutex_trylock(&mutex->mutex);
    if (result == 0) {
        return XGL_OK;
    } else {
        return XGL_ERR_BUSY;
    }
}

/**
 * \brief           Unlock POSIX mutex
 */
xgl_error_t xgl_mutex_unlock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    int result = pthread_mutex_unlock(&mutex->mutex);
    if (result != 0) {
        return XGL_ERR_BUSY;
    }

    return XGL_OK;
}

/**
 * \brief           Destroy POSIX mutex
 */
void xgl_mutex_destroy(xgl_mutex_t* mutex) {
    if (mutex == NULL || !mutex->initialized) {
        return;
    }

    pthread_mutex_destroy(&mutex->mutex);
    mutex->initialized = false;
}

#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_WINDOWS)

/* Implemented in xgl_mutex_windows.c. */

/*---------------------------------------------------------------------------*/
/* FreeRTOS Implementation                                                   */
/*---------------------------------------------------------------------------*/

#elif defined(XGL_THREAD_SAFE) && defined(XGL_PLATFORM_FREERTOS)

/**
 * \brief           Initialize FreeRTOS mutex
 */
xgl_error_t xgl_mutex_init(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Create recursive mutex using static allocation */
    mutex->handle = xSemaphoreCreateRecursiveMutexStatic(&mutex->buffer);
    if (mutex->handle == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    mutex->initialized = true;
    return XGL_OK;
}

/**
 * \brief           Lock FreeRTOS mutex
 */
xgl_error_t xgl_mutex_lock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    /* Wait indefinitely for mutex */
    BaseType_t result = xSemaphoreTakeRecursive(mutex->handle, portMAX_DELAY);
    if (result != pdTRUE) {
        return XGL_ERR_BUSY;
    }

    return XGL_OK;
}

/**
 * \brief           Try to lock FreeRTOS mutex
 */
xgl_error_t xgl_mutex_trylock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    /* Try to take mutex without blocking */
    BaseType_t result = xSemaphoreTakeRecursive(mutex->handle, 0);
    if (result == pdTRUE) {
        return XGL_OK;
    } else {
        return XGL_ERR_BUSY;
    }
}

/**
 * \brief           Unlock FreeRTOS mutex
 */
xgl_error_t xgl_mutex_unlock(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!mutex->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    BaseType_t result = xSemaphoreGiveRecursive(mutex->handle);
    if (result != pdTRUE) {
        return XGL_ERR_BUSY;
    }

    return XGL_OK;
}

/**
 * \brief           Destroy FreeRTOS mutex
 */
void xgl_mutex_destroy(xgl_mutex_t* mutex) {
    if (mutex == NULL || !mutex->initialized) {
        return;
    }

    /* FreeRTOS static semaphores don't need explicit deletion */
    /* Just mark as uninitialized */
    mutex->initialized = false;
    mutex->handle = NULL;
}

/*---------------------------------------------------------------------------*/
/* Bare-Metal / No-Op Implementation                                         */
/*---------------------------------------------------------------------------*/

#else

/**
 * \brief           Initialize no-op mutex
 */
xgl_error_t xgl_mutex_init(xgl_mutex_t* mutex) {
    if (mutex == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* No-op for bare-metal */
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

    /* No-op for bare-metal */
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

    /* No-op for bare-metal - always succeeds */
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

    /* No-op for bare-metal */
    (void)mutex;
    return XGL_OK;
}

/**
 * \brief           Destroy no-op mutex
 */
void xgl_mutex_destroy(xgl_mutex_t* mutex) {
    /* No-op for bare-metal */
    (void)mutex;
}

#endif /* Platform-specific implementations */
