/**
 * \file            xgl_datalink_metadata.h
 * \brief           Internal datalink RX metadata validation
 */

#ifndef XGL_DATALINK_METADATA_H
#define XGL_DATALINK_METADATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xgl/internal/xgl_wire.h"
#include "xgl/xgl_error.h"

typedef struct {
    xgl_wire_frame_view_t frame;
    bool header_crc_failed;
    bool frame_crc_failed;
} xgl_datalink_rx_metadata_t;

xgl_error_t
xgl_datalink_decode_rx_metadata(const uint8_t* frame_buffer, size_t frame_len,
                                xgl_datalink_rx_metadata_t* metadata);

#ifdef __cplusplus
}
#endif

#endif /* XGL_DATALINK_METADATA_H */
