/**
 * \file            xgl_wire_ext.c
 * \brief           Wire-format extension value encoding primitives
 */

#include <xgl/internal/xgl_wire.h>
#include <xgl/internal/xgl_serialize.h>

static void wire_serialize_u64_le(uint8_t* buffer, uint64_t value) {
    for (size_t i = 0; i < 8U; ++i) {
        buffer[i] = (uint8_t)((value >> (8U * i)) & 0xFFU);
    }
}

static uint64_t wire_deserialize_u64_le(const uint8_t* buffer) {
    uint64_t value = 0U;
    for (size_t i = 0; i < 8U; ++i) {
        value |= ((uint64_t)buffer[i]) << (8U * i);
    }
    return value;
}

xgl_error_t xgl_wire_encode_fragment_ext_value(uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t message_id,
                                               uint32_t fragment_offset,
                                               uint32_t message_len,
                                               size_t* bytes_written) {
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < XGL_FRAGMENT_EXT_VALUE_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_serialize_u32_le(&buffer[0], message_id);
    xgl_serialize_u32_le(&buffer[4], fragment_offset);
    xgl_serialize_u32_le(&buffer[8], message_len);
    *bytes_written = XGL_FRAGMENT_EXT_VALUE_SIZE;

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_fragment_ext_value(const uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t* message_id,
                                               uint32_t* fragment_offset,
                                               uint32_t* message_len) {
    if (buffer == NULL || message_id == NULL || fragment_offset == NULL ||
        message_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != XGL_FRAGMENT_EXT_VALUE_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    *message_id = xgl_deserialize_u32_le(&buffer[0]);
    *fragment_offset = xgl_deserialize_u32_le(&buffer[4]);
    *message_len = xgl_deserialize_u32_le(&buffer[8]);

    return XGL_OK;
}

xgl_error_t xgl_wire_encode_session_ext_value(uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t session_epoch,
                                              uint64_t incarnation_id,
                                              size_t* bytes_written) {
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < XGL_SESSION_EXT_VALUE_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_serialize_u32_le(&buffer[0], session_epoch);
    wire_serialize_u64_le(&buffer[4], incarnation_id);
    *bytes_written = XGL_SESSION_EXT_VALUE_SIZE;

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_session_ext_value(const uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t* session_epoch,
                                              uint64_t* incarnation_id) {
    if (buffer == NULL || session_epoch == NULL || incarnation_id == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != XGL_SESSION_EXT_VALUE_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    *session_epoch = xgl_deserialize_u32_le(&buffer[0]);
    *incarnation_id = wire_deserialize_u64_le(&buffer[4]);

    return XGL_OK;
}

xgl_error_t xgl_wire_encode_security_ext_value(uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t key_id,
                                               uint64_t nonce_id,
                                               uint8_t tag_len,
                                               size_t* bytes_written) {
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tag_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (buffer_size < 13U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_serialize_u32_le(&buffer[0], key_id);
    wire_serialize_u64_le(&buffer[4], nonce_id);
    buffer[12] = tag_len;
    *bytes_written = 13U;

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_security_ext_value(const uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t* key_id,
                                               uint64_t* nonce_id,
                                               uint8_t* tag_len) {
    if (buffer == NULL || key_id == NULL || nonce_id == NULL || tag_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != 13U) {
        return XGL_ERR_INVALID_FRAME;
    }

    *key_id = xgl_deserialize_u32_le(&buffer[0]);
    *nonce_id = wire_deserialize_u64_le(&buffer[4]);
    *tag_len = buffer[12];
    if (*tag_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}

xgl_error_t xgl_wire_encode_route_ext_value(uint8_t* buffer,
                                            size_t buffer_size,
                                            uint16_t previous_hop,
                                            uint16_t next_hop,
                                            uint32_t route_epoch,
                                            uint16_t metric,
                                            size_t* bytes_written) {
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < 10U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_serialize_u16_le(&buffer[0], previous_hop);
    xgl_serialize_u16_le(&buffer[2], next_hop);
    xgl_serialize_u32_le(&buffer[4], route_epoch);
    xgl_serialize_u16_le(&buffer[8], metric);
    *bytes_written = 10U;

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_route_ext_value(const uint8_t* buffer,
                                            size_t buffer_size,
                                            uint16_t* previous_hop,
                                            uint16_t* next_hop,
                                            uint32_t* route_epoch,
                                            uint16_t* metric) {
    if (buffer == NULL || previous_hop == NULL || next_hop == NULL ||
        route_epoch == NULL || metric == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != 10U) {
        return XGL_ERR_INVALID_FRAME;
    }

    *previous_hop = xgl_deserialize_u16_le(&buffer[0]);
    *next_hop = xgl_deserialize_u16_le(&buffer[2]);
    *route_epoch = xgl_deserialize_u32_le(&buffer[4]);
    *metric = xgl_deserialize_u16_le(&buffer[8]);

    return XGL_OK;
}
