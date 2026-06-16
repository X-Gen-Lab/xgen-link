/**
 * \file            xgl_frame_zerocopy.c
 * \brief           Zero-copy frame building implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_crc.h>
#include <xgl/internal/xgl_serialize.h>
#include <xgl/internal/xgl_wire.h>

#define XGL_FRAME_DEFAULT_TTL   8U

xgl_error_t xgl_frame_build_zerocopy(uint8_t* buffer,
                                     size_t buffer_size,
                                     size_t data_offset,
                                     size_t data_len,
                                     uint16_t source_id,
                                     uint16_t target_id,
                                     uint8_t data_type,
                                     uint32_t packet_number,
                                     bool reliable,
                                     uint8_t priority,
                                     size_t* frame_len) {
    if (buffer == NULL || frame_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (data_len > UINT16_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t app_type_ext_len = (data_type != 0U) ? XGL_DATA_TYPE_EXT_SIZE : 0U;
    size_t header_len = XGL_WIRE_BASE_HEADER_SIZE + app_type_ext_len;
    if (header_len > UINT8_MAX || data_offset != header_len) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t required_size = data_offset + data_len + XGL_CRC16_SIZE;
    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t flags = reliable ? XGL_WIRE_FLAG_ACK_ELICITING : 0U;
    if (app_type_ext_len > 0U) {
        flags |= XGL_WIRE_FLAG_HAS_EXTENSIONS;
        size_t app_ext_written = 0U;
        xgl_error_t ext_err = xgl_wire_encode_ext(&buffer[XGL_WIRE_BASE_HEADER_SIZE],
                                                  buffer_size - XGL_WIRE_BASE_HEADER_SIZE,
                                                  XGL_WIRE_EXT_DATA_TYPE,
                                                  &data_type,
                                                  1U,
                                                  &app_ext_written);
        if (ext_err != XGL_OK) {
            return ext_err;
        }
        if (app_ext_written != app_type_ext_len) {
            return XGL_ERR_INVALID_FRAME;
        }
    }

    xgl_wire_header_t wire = {
        .version = XGL_WIRE_VERSION,
        .header_len = (uint8_t)header_len,
        .packet_type = XGL_PACKET_TYPE_DATA,
        .flags = flags,
        .ttl = XGL_FRAME_DEFAULT_TTL,
        .traffic_class = (uint8_t)((reliable ? XGL_RELIABILITY_ACK_ELICITING : 0U) |
                                   (priority & XGL_TRAFFIC_PRIORITY_MASK)),
        .source_id = source_id,
        .target_id = target_id,
        .connection_id = 0,
        .packet_number = packet_number,
        .payload_len = (uint16_t)data_len,
        .header_crc16 = 0
    };

    xgl_error_t err = xgl_wire_encode_header(buffer,
                                             XGL_WIRE_BASE_HEADER_SIZE,
                                             &wire);
    if (err != XGL_OK) {
        return err;
    }

    size_t crc_offset = data_offset + data_len;
    uint16_t crc16 = xgl_crc16_modbus(buffer, crc_offset);
    xgl_serialize_u16_le(&buffer[crc_offset], crc16);

    *frame_len = required_size;
    return XGL_OK;
}
