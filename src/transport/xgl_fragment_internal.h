/**
 * \file            xgl_fragment_internal.h
 * \brief           Private fragment reassembly helpers
 */

#ifndef XGL_FRAGMENT_INTERNAL_H
#define XGL_FRAGMENT_INTERNAL_H

#include <xgl/internal/xgl_fragment.h>

xgl_error_t fragment_insert_received_range(xgl_reassembly_buffer_t* buffer,
                                           size_t start,
                                           size_t end);

#endif /* XGL_FRAGMENT_INTERNAL_H */
