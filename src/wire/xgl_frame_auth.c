/**
 * \file            xgl_frame_auth.c
 * \brief           Authenticated frame serialization
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_crc.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_serialize.h>
#include <xgl/internal/xgl_wire.h>
#include <string.h>

static xgl_error_t encode_authenticated_header(uint8_t* buffer,
                                               size_t buffer_size,
                                               const xgl_frame_t* frame,
                                               uint32_t key_id,
                                               uint8_t tag_len,
                                               size_t* header_len) {
    size_t base_ext_len = frame->extensions_len;
    if (base_ext_len > UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if (buffer_size < XGL_WIRE_BASE_HEADER_SIZE + base_ext_len +
                      XGL_WIRE_EXT_HEADER_SIZE + 13U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if (frame->extensions != NULL && base_ext_len > 0U) {
        memcpy(&buffer[XGL_WIRE_BASE_HEADER_SIZE],
               frame->extensions,
               base_ext_len);
    }

    uint8_t security_value[13] = {0};
    size_t security_value_len = 0;
    xgl_error_t err = xgl_wire_encode_security_ext_value(security_value,
                                                         sizeof(security_value),
                                                         key_id,
                                                         frame->header.packet_number,
                                                         tag_len,
                                                         &security_value_len);
    if (err != XGL_OK) {
        return err;
    }

    size_t security_ext_len = 0;
    err = xgl_wire_encode_ext(&buffer[XGL_WIRE_BASE_HEADER_SIZE + base_ext_len],
                              buffer_size - XGL_WIRE_BASE_HEADER_SIZE - base_ext_len,
                              XGL_WIRE_EXT_SECURITY,
                              security_value,
                              security_value_len,
                              &security_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    size_t produced_header_len = XGL_WIRE_BASE_HEADER_SIZE + base_ext_len + security_ext_len;
    if (produced_header_len > UINT8_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_wire_header_t wire = frame->header;
    wire.version = XGL_WIRE_VERSION;
    wire.header_len = (uint8_t)produced_header_len;
    wire.flags = (uint8_t)(wire.flags |
                           XGL_WIRE_FLAG_HAS_EXTENSIONS |
                           XGL_WIRE_FLAG_AUTHENTICATED);
    wire.payload_len = (uint16_t)frame->payload_len;
    wire.header_crc16 = 0;

    err = xgl_wire_encode_header(buffer, buffer_size, &wire);
    if (err != XGL_OK) {
        return err;
    }

    *header_len = produced_header_len;
    return XGL_OK;
}

xgl_error_t xgl_frame_serialize_authenticated(uint8_t* buffer,
                                              size_t buffer_size,
                                              const xgl_frame_t* frame,
                                              uint32_t key_id,
                                              const xgl_auth_provider_t* provider,
                                              size_t* bytes_written) {
    if (buffer == NULL || frame == NULL || provider == NULL ||
        provider->sign == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (frame->payload_len > UINT16_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (provider->tag_len == 0U || provider->tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t header_len = 0;
    xgl_error_t err = encode_authenticated_header(buffer,
                                                  buffer_size,
                                                  frame,
                                                  key_id,
                                                  (uint8_t)provider->tag_len,
                                                  &header_len);
    if (err != XGL_OK) {
        return err;
    }

    if (buffer_size < header_len + frame->payload_len +
                      provider->tag_len + XGL_CRC16_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (frame->payload != NULL && frame->payload_len > 0U) {
        memcpy(&buffer[header_len], frame->payload, frame->payload_len);
    }

    size_t frame_len_without_crc = 0;
    err = xgl_wire_append_auth_trailer(buffer,
                                       buffer_size - XGL_CRC16_SIZE,
                                       header_len,
                                       frame->payload_len,
                                       key_id,
                                       provider,
                                       &frame_len_without_crc);
    if (err != XGL_OK) {
        return err;
    }
    if (frame_len_without_crc != header_len + frame->payload_len +
                                 provider->tag_len ||
        frame_len_without_crc + XGL_CRC16_SIZE > buffer_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint16_t crc16 = xgl_crc16_modbus(buffer, frame_len_without_crc);
    xgl_serialize_u16_le(&buffer[frame_len_without_crc], crc16);
    *bytes_written = frame_len_without_crc + XGL_CRC16_SIZE;
    return XGL_OK;
}
