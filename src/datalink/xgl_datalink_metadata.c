/**
 * \file            xgl_datalink_metadata.c
 * \brief           Datalink RX metadata validation
 */

#include "xgl/internal/xgl_datalink_metadata.h"
#include "xgl/internal/xgl_crc.h"
#include "xgl/internal/xgl_frame.h"
#include "xgl/internal/xgl_serialize.h"
#include "xgl/xgl_config.h"

#include <string.h>

static xgl_error_t datalink_decode_rx_extensions(const uint8_t* frame_buffer,
                                                 const xgl_wire_header_t* header,
                                                 bool auth_required,
                                                 uint32_t required_auth_key_id,
                                                 xgl_datalink_rx_metadata_t* metadata) {
    if (header->header_len <= XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_OK;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(
        &cursor,
        &frame_buffer[XGL_WIRE_BASE_HEADER_SIZE],
        (size_t)header->header_len - XGL_WIRE_BASE_HEADER_SIZE
    );
    if (err != XGL_OK) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type == XGL_WIRE_EXT_SECURITY) {
            uint64_t nonce_id = 0U;
            uint8_t tag_len = 0U;
            err = xgl_wire_decode_security_ext_value(ext.value,
                                                     ext.len,
                                                     &metadata->auth_key_id,
                                                     &nonce_id,
                                                     &tag_len);
            if (err != XGL_OK || tag_len == 0U) {
                return XGL_ERR_INVALID_FRAME;
            }
            (void)nonce_id;
            if (auth_required && metadata->auth_key_id != required_auth_key_id) {
                metadata->auth_key_rejected = true;
                return XGL_ERR_INVALID_FRAME;
            }
            metadata->auth_tag_len = tag_len;
            metadata->has_security_ext = true;
            continue;
        }

        if (ext.type == XGL_WIRE_EXT_SESSION) {
            uint64_t incarnation_id = 0U;
            err = xgl_wire_decode_session_ext_value(ext.value,
                                                    ext.len,
                                                    &metadata->session_epoch,
                                                    &incarnation_id);
            if (err != XGL_OK) {
                return XGL_ERR_INVALID_FRAME;
            }
            (void)incarnation_id;
        }
    }

    return (err == XGL_ERR_NOT_FOUND) ? XGL_OK : XGL_ERR_INVALID_FRAME;
}

xgl_error_t xgl_datalink_decode_rx_metadata(const uint8_t* frame_buffer,
                                            size_t frame_len,
                                            bool auth_required,
                                            uint32_t required_auth_key_id,
                                            xgl_datalink_rx_metadata_t* metadata) {
    if (frame_buffer == NULL || metadata == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    memset(metadata, 0, sizeof(*metadata));

    size_t min_frame_size = XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE;
    if (frame_len < min_frame_size ||
        frame_len > XGL_DATALINK_MAX_FRAME_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (frame_buffer[0] != XGL_WIRE_MAGIC_0 ||
        frame_buffer[1] != XGL_WIRE_MAGIC_1) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (xgl_wire_decode_header(&metadata->header, frame_buffer, frame_len) != XGL_OK) {
        metadata->header_crc_failed = true;
        return XGL_ERR_CRC_FAILED;
    }

    xgl_error_t err = datalink_decode_rx_extensions(frame_buffer,
                                                    &metadata->header,
                                                    auth_required,
                                                    required_auth_key_id,
                                                    metadata);
    if (err != XGL_OK) {
        return err;
    }

    metadata->authenticated =
        (metadata->header.flags & XGL_WIRE_FLAG_AUTHENTICATED) != 0U;
    metadata->should_verify_auth =
        metadata->authenticated || metadata->has_security_ext;

    size_t crc_offset = frame_len - XGL_CRC16_SIZE;
    uint16_t calculated_crc = xgl_crc16_modbus(frame_buffer, crc_offset);
    uint16_t received_crc = xgl_deserialize_u16_le(&frame_buffer[crc_offset]);
    if (calculated_crc != received_crc) {
        metadata->frame_crc_failed = true;
        return XGL_ERR_CRC_FAILED;
    }

    metadata->payload_len = metadata->header.payload_len;
    size_t expected_frame_len = (size_t)metadata->header.header_len +
                                metadata->payload_len +
                                (size_t)metadata->auth_tag_len +
                                XGL_CRC16_SIZE;
    if (frame_len != expected_frame_len) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}
