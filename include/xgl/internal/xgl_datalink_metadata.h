/**
 * \file            xgl_datalink_metadata.h
 * \brief           Internal datalink RX metadata validation
 */

#ifndef XGL_DATALINK_METADATA_H
#define XGL_DATALINK_METADATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "xgl/xgl_error.h"
#include "xgl/internal/xgl_wire.h"

typedef struct {
    xgl_wire_header_t header;
    uint8_t auth_tag_len;
    bool has_security_ext;
    bool authenticated;
    bool should_verify_auth;
    bool header_crc_failed;
    bool frame_crc_failed;
    bool auth_key_rejected;
    uint32_t auth_key_id;
    uint32_t session_epoch;
    size_t payload_len;
} xgl_datalink_rx_metadata_t;

xgl_error_t xgl_datalink_decode_rx_metadata(const uint8_t* frame_buffer,
                                            size_t frame_len,
                                            bool auth_required,
                                            uint32_t required_auth_key_id,
                                            xgl_datalink_rx_metadata_t* metadata);

#ifdef __cplusplus
}
#endif

#endif /* XGL_DATALINK_METADATA_H */
