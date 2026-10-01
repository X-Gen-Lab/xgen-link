/**
 * \file            xgl_fragment_internal.h
 * \brief           Private fragment reassembly helpers
 */

#ifndef XGL_FRAGMENT_INTERNAL_H
#define XGL_FRAGMENT_INTERNAL_H

#include <xgl/internal/xgl_fragment.h>

/**
 * \brief           Free a buffer through its owning manager
 * \param[in,out]   manager: Live owning manager when buffer is non-NULL
 * \param[in,out]   buffer: Detached buffer to release, or NULL
 */
void fragment_free_reassembly_buffer(xgl_fragment_manager_t* manager,
                                     xgl_reassembly_buffer_t* buffer);

xgl_error_t fragment_insert_received_range(xgl_reassembly_buffer_t* buffer,
                                           size_t start, size_t end);

#endif /* XGL_FRAGMENT_INTERNAL_H */
