/**
 * \file            xgl_alloc.h
 * \brief           Allocator-backed instance initialization
 * \author          X-Gen Lab
 */

#ifndef XGL_ALLOC_H
#define XGL_ALLOC_H

#include <xgl/xgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           Explicit synchronous allocation service
 * \note            The descriptor and its context must outlive the instance.
 *                  The alloc callback receives a nonzero size and returns a
 *                  block aligned to xgl_max_align_t, or NULL on failure.
 *                  The free callback receives live blocks from the same
 *                  service, once per allocation. XGL skips zero-size requests
 *                  and NULL releases.
 */
typedef struct {
    void* ctx; /**< Borrowed context passed to both callbacks. */
    void* (*alloc)(void* ctx, size_t size); /**< Allocate an aligned block. */
    void (*free)(void* ctx, void* ptr);     /**< Release a live block. */
} xgl_allocator_t;

/**
 * \brief           Allocate and initialize an instance in one operation
 * \param[in]       config: Immutable configuration borrowed until destruction
 * \param[in]       allocator: Borrowed allocator, or NULL to select the
 *                  compiled backend policy
 * \param[out]      handle: Ready instance, cleared on failure
 * \return          XGL_OK or a validation, allocation or startup error
 * \note            Destruction frees the workspace through its backend.
 */
xgl_error_t xgl_init_allocated(const xgl_config_t* config,
                               const xgl_allocator_t* allocator,
                               xgl_handle_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* XGL_ALLOC_H */
