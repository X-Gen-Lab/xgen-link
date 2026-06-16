/**
 * \file            xgl_network_metadata.c
 * \brief           Network frame metadata decoding
 */

#include "xgl/internal/xgl_network_metadata.h"
#include "xgl/internal/xgl_frame.h"

#include <string.h>

xgl_error_t xgl_network_decode_ext_metadata(const uint8_t* extensions,
                                            size_t extensions_len,
                                            xgl_network_ext_metadata_t* metadata) {
    if (metadata == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    memset(metadata, 0, sizeof(*metadata));
    if (extensions == NULL || extensions_len == 0U) {
        return XGL_OK;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(&cursor, extensions, extensions_len);
    if (err != XGL_OK) {
        return err;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type == XGL_WIRE_EXT_DATA_TYPE) {
            if (metadata->data_type_found || ext.len != 1U || ext.value == NULL) {
                return XGL_ERR_INVALID_FRAME;
            }
            metadata->data_type = ext.value[0];
            metadata->data_type_found = true;
            continue;
        }

        if (ext.type == XGL_WIRE_EXT_SESSION) {
            if (metadata->session_epoch_found) {
                return XGL_ERR_INVALID_FRAME;
            }

            uint64_t incarnation_id = 0U;
            err = xgl_wire_decode_session_ext_value(ext.value,
                                                    ext.len,
                                                    &metadata->session_epoch,
                                                    &incarnation_id);
            if (err != XGL_OK) {
                return err;
            }
            (void)incarnation_id;
            metadata->session_epoch_found = true;
        }
    }

    return (err == XGL_ERR_NOT_FOUND) ? XGL_OK : err;
}

xgl_error_t xgl_network_decode_frame_metadata(const uint8_t* frame_buf,
                                              size_t frame_len,
                                              xgl_network_frame_metadata_t* metadata) {
    if (frame_buf == NULL || metadata == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (frame_len < XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    memset(metadata, 0, sizeof(*metadata));
    xgl_error_t err = xgl_wire_decode_header(&metadata->header,
                                             frame_buf,
                                             frame_len);
    if (err != XGL_OK) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (metadata->header.header_len > XGL_WIRE_BASE_HEADER_SIZE) {
        metadata->extensions = frame_buf + XGL_WIRE_BASE_HEADER_SIZE;
        metadata->extensions_len =
            (size_t)metadata->header.header_len - XGL_WIRE_BASE_HEADER_SIZE;
    }

    xgl_network_ext_metadata_t ext_metadata;
    err = xgl_network_decode_ext_metadata(metadata->extensions,
                                          metadata->extensions_len,
                                          &ext_metadata);
    if (err != XGL_OK) {
        return err;
    }
    metadata->data_type = ext_metadata.data_type;
    metadata->session_epoch = ext_metadata.session_epoch;

    metadata->payload = frame_buf + metadata->header.header_len;
    metadata->payload_len = metadata->header.payload_len;
    if (frame_len < (size_t)metadata->header.header_len +
                    metadata->payload_len +
                    XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    uint8_t traffic_reliability =
        (uint8_t)(metadata->header.traffic_class & XGL_RELIABILITY_CLASS_MASK);
    if (traffic_reliability == XGL_RELIABILITY_ACK_ELICITING ||
        (metadata->header.flags & XGL_WIRE_FLAG_ACK_ELICITING) != 0U) {
        metadata->reliable = XGL_RELIABILITY_ACK_ELICITING;
    } else if (traffic_reliability == XGL_RELIABILITY_ACK_ONLY ||
               metadata->header.packet_type == XGL_PACKET_TYPE_ACK) {
        metadata->reliable = XGL_RELIABILITY_ACK_ONLY;
    }

    metadata->fragment =
        ((metadata->header.flags & XGL_WIRE_FLAG_FRAGMENTED) != 0U) ? 1U : 0U;
    metadata->priority =
        (uint8_t)((metadata->header.traffic_class & XGL_TRAFFIC_PRIORITY_MASK) >>
                  XGL_TRAFFIC_PRIORITY_SHIFT);
    return XGL_OK;
}
