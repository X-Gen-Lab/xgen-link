/**
 * \file            xgl_allocator.c
 * \brief           Custom allocator support implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_allocator.h>
#include "xgl_allocator_internal.h"
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
/* Default Allocator Implementation                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default malloc wrapper
 */
#if XGL_ALLOW_FALLBACK_MALLOC
static void* default_malloc(size_t size) {
    return malloc(size);
}

/**
 * \brief           Default free wrapper
 */
static void default_free(void* ptr) {
    free(ptr);
}

/**
 * \brief           Default allocator instance
 */
static xgl_allocator_t default_allocator = {
    .malloc = default_malloc,
    .free = default_free,
    .user_data = NULL
};
#endif

/**
 * \brief           Get default allocator
 */
xgl_allocator_t* xgl_allocator_get_default(void) {
#if XGL_ALLOW_FALLBACK_MALLOC
    return &default_allocator;
#else
    return NULL;
#endif
}

/*---------------------------------------------------------------------------*/
/* Allocator Wrapper Functions                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using provided allocator
 * \details         If allocator is NULL, uses default malloc/free only when
 *                  XGL_ALLOW_FALLBACK_MALLOC is enabled.
 */
void* xgl_alloc(xgl_allocator_t* allocator, size_t size) {
    /* Validate size */
    if (size == 0) {
        return NULL;
    }

    /* Use default allocator if none provided */
    if (allocator == NULL) {
#if XGL_ALLOW_FALLBACK_MALLOC
        allocator = &default_allocator;
#else
        return NULL;
#endif
    }

    /* Validate allocator has malloc function */
    if (allocator->malloc == NULL) {
        return NULL;
    }

    if (xgl_tracking_allocator_is_alloc_callback(allocator)) {
        return xgl_tracking_allocator_alloc_from_interface(allocator, size);
    }

    /* Allocate memory */
    return allocator->malloc(size);
}

/**
 * \brief           Free memory using provided allocator
 * \details         If allocator is NULL, uses default malloc/free only when
 *                  XGL_ALLOW_FALLBACK_MALLOC is enabled.
 */
void xgl_free(xgl_allocator_t* allocator, void* ptr) {
    /* Nothing to free */
    if (ptr == NULL) {
        return;
    }

    /* Use default allocator if none provided */
    if (allocator == NULL) {
#if XGL_ALLOW_FALLBACK_MALLOC
        allocator = &default_allocator;
#else
        return;
#endif
    }

    /* Validate allocator has free function */
    if (allocator->free == NULL) {
        return;
    }

    if (xgl_tracking_allocator_is_free_callback(allocator)) {
        xgl_tracking_allocator_free_from_interface(allocator, ptr);
        return;
    }

    /* Free memory */
    allocator->free(ptr);
}
