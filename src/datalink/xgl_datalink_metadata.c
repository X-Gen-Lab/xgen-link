/**
 * \file            xgl_datalink_metadata.c
 * \brief           Datalink policy around the shared validated wire view
 */

#include "xgl/internal/xgl_datalink_metadata.h"

#include <string.h>

#include "xgl/xgl_config.h"

/**
 * \brief           Validate frame structure and expose wire diagnostics
 * \param[in]       frame_buffer: Complete frame bytes
 * \param[in]       frame_len: Available bytes including the final CRC
 * \param[out]      metadata: Borrowed view and diagnostics for the caller
 * \return          XGL_OK on success, error code otherwise
 * \note            Endpoint verification is performed before network local
 * delivery.
 */
xgl_error_t
xgl_datalink_decode_rx_metadata(const uint8_t* frame_buffer, size_t frame_len,
                                xgl_datalink_rx_metadata_t* metadata) {
    if (metadata == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(metadata, 0, sizeof(*metadata));
    if (frame_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (frame_len > XGL_DATALINK_MAX_FRAME_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }
    xgl_wire_decode_status_t status;
    xgl_error_t err = xgl_wire_decode_frame(&metadata->frame, frame_buffer,
                                            frame_len, &status);
    metadata->header_crc_failed = status == XGL_WIRE_DECODE_HEADER_CRC;
    metadata->frame_crc_failed = status == XGL_WIRE_DECODE_FRAME_CRC;
    if (err != XGL_OK) {
        return err;
    }
    return XGL_OK;
}
