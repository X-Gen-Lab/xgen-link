/**
 * \file            xgl_inplace.h
 * \brief           Synchronous caller-buffer transmission
 * \author          X-Gen Lab
 */

#ifndef XGL_INPLACE_H
#define XGL_INPLACE_H

#include <xgl/xgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           Synchronous non-reliable transmission in caller storage
 */
typedef struct {
    uint8_t* buffer;        /**< Borrowed complete frame buffer */
    size_t buffer_size;     /**< Available frame bytes */
    size_t data_offset;     /**< Exact payload offset from the layout query */
    size_t data_len;        /**< Payload length in bytes */
    uint16_t target_id;     /**< Destination node */
    uint8_t data_type;      /**< Optional application type, or zero */
    uint8_t priority;       /**< Wire priority from 0 through 7 */
    uint32_t connection_id; /**< Connection identity */
    uint32_t session_epoch; /**< Scope epoch, zero omits SESSION_EXT */
} xgl_tx_zerocopy_request_t;

/** \brief           Exact storage required for one synchronous frame. */
typedef struct {
    size_t
        payload_offset; /**< Header bytes including all selected extensions */
    size_t frame_size; /**< Header, payload, authentication tag and final CRC */
} xgl_tx_frame_layout_t;

/**
 * \brief           Query exact frame storage without transmitting or allocating
 * \param[in]       handle: Initialized instance, serialized by the caller
 * \param[in]       request: Scope and send metadata; buffer, buffer_size and
 *                  data_offset are ignored, data_len must be nonzero
 * \param[out]      layout: Exact headroom and frame size, cleared on failure
 * \return          XGL_OK or an argument, route, MTU, security
 *                  or terminal-scope error
 * \note            Does not call the provider or PHY, reserve storage or
 * consume a security sequence. An authenticated scope must already be
 *                  installed. The eventual send revalidates current state.
 */
xgl_error_t xgl_send_zerocopy_layout(xgl_handle_t handle,
                                     const xgl_tx_zerocopy_request_t* request,
                                     xgl_tx_frame_layout_t* layout);

/**
 * \brief           Send one caller-owned frame within an explicit peer scope
 * \param[in]       handle: Initialized instance, serialized by the caller
 * \param[in]       request: Writable frame storage and exact connection/epoch
 * \param[in]       now_ms: Same monotonic clock used by xgl_step()
 * \return          XGL_OK or a validation, capacity, security or driver error
 * \note            Use xgl_send_zerocopy_layout() before filling the payload.
 *                  The payload stays at its original address. All callbacks
 *                  borrow it synchronously; no pointer may outlive this call.
 *                  The submission is non-reliable.
 */
xgl_error_t
xgl_send_zerocopy_scoped_at(xgl_handle_t handle,
                            const xgl_tx_zerocopy_request_t* request,
                            uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* XGL_INPLACE_H */
