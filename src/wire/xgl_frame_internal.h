/**
 * \file            xgl_frame_internal.h
 * \brief           Synchronous encoding of an already measured frame
 * \author          X-Gen Lab
 */

#ifndef XGL_FRAME_INTERNAL_H
#define XGL_FRAME_INTERNAL_H

#include "wire/xgl_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           Encode with the layout validated in the current send call
 * \param[out]      buffer: Non-NULL destination with frame_len bytes available
 * \param[in]       frame: Logical frame and already validated borrowed spans
 * \param[in]       layout: Successful measurement of that exact frame and tag
 * \return          XGL_OK, or a wire header/extension encoding error
 * \note            Keep the logical frame, TLVs and measured lengths unchanged
 *                  from measurement through encoding. An in-place payload
 *                  starts at layout->header_len. The caller supplies the
 *                  optional tag and finalizes the frame CRC.
 */
xgl_error_t xgl_frame_encode_measured(uint8_t* buffer, const xgl_frame_t* frame,
                                      const xgl_frame_layout_t* layout);

#ifdef __cplusplus
}
#endif

#endif /* XGL_FRAME_INTERNAL_H */
