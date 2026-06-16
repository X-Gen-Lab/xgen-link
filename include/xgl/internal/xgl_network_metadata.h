/**
 * \file            xgl_network_metadata.h
 * \brief           Internal network frame metadata decoding
 */

#ifndef XGL_NETWORK_METADATA_H
#define XGL_NETWORK_METADATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "xgl/xgl_error.h"
#include "xgl/internal/xgl_wire.h"

typedef struct {
    uint8_t data_type;
    bool data_type_found;
    uint32_t session_epoch;
    bool session_epoch_found;
} xgl_network_ext_metadata_t;

typedef struct {
    xgl_wire_header_t header;
    uint8_t data_type;
    uint32_t session_epoch;
    const uint8_t* extensions;
    size_t extensions_len;
    const uint8_t* payload;
    size_t payload_len;
    uint8_t reliable;
    uint8_t fragment;
    uint8_t priority;
} xgl_network_frame_metadata_t;

xgl_error_t xgl_network_decode_ext_metadata(const uint8_t* extensions,
                                            size_t extensions_len,
                                            xgl_network_ext_metadata_t* metadata);

xgl_error_t xgl_network_decode_frame_metadata(const uint8_t* frame_buf,
                                              size_t frame_len,
                                              xgl_network_frame_metadata_t* metadata);

#ifdef __cplusplus
}
#endif

#endif /* XGL_NETWORK_METADATA_H */
