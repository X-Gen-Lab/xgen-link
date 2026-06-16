/**
 * \file            xgl_mutex_guard.c
 * \brief           Mutex guard helper implementation
 * \author          X-Gen Lab
 */

#include "xgl/internal/xgl_mutex.h"
#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* Common Implementation (All Platforms)                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Create mutex lock guard
 */
xgl_mutex_guard_t xgl_mutex_guard_lock(xgl_mutex_t* mutex) {
    xgl_mutex_guard_t guard;
    guard.mutex = mutex;
    guard.locked = false;

    if (mutex != NULL) {
        if (xgl_mutex_lock(mutex) == XGL_OK) {
            guard.locked = true;
        }
    }

    return guard;
}

/**
 * \brief           Release mutex lock guard
 */
void xgl_mutex_guard_unlock(xgl_mutex_guard_t* guard) {
    if (guard != NULL && guard->locked && guard->mutex != NULL) {
        xgl_mutex_unlock(guard->mutex);
        guard->locked = false;
    }
}
