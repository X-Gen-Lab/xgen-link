/**
 * \file            xgl_wire.c
 * \brief           Production wire-format encoding primitives
 */

#include <xgl/xgl_wire.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <string.h>

static uint16_t wire_header_crc16(const uint8_t* buffer) {
    uint8_t crc_input[XGL_WIRE_BASE_HEADER_SIZE];
    memcpy(crc_input, buffer, XGL_WIRE_BASE_HEADER_SIZE);
    crc_input[22] = 0;
    crc_input[23] = 0;
    return xgl_crc16_modbus(crc_input, XGL_WIRE_BASE_HEADER_SIZE);
}

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

xgl_error_t xgl_wire_encode_header(uint8_t* buffer,
                                   size_t buffer_size,
                                   const xgl_wire_header_t* header) {
    if (buffer == NULL || header == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (header->version != XGL_WIRE_VERSION ||
        header->header_len < XGL_WIRE_BASE_HEADER_SIZE ||
        header->packet_type == XGL_PACKET_TYPE_INVALID ||
        header->source_id == 0U ||
        header->target_id == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    memset(buffer, 0, XGL_WIRE_BASE_HEADER_SIZE);
    buffer[0] = XGL_WIRE_MAGIC_0;
    buffer[1] = XGL_WIRE_MAGIC_1;
    buffer[2] = header->version;
    buffer[3] = header->header_len;
    buffer[4] = header->packet_type;
    buffer[5] = header->flags;
    buffer[6] = header->ttl;
    buffer[7] = header->traffic_class;
    xgl_serialize_u16_le(&buffer[8], header->source_id);
    xgl_serialize_u16_le(&buffer[10], header->target_id);
    xgl_serialize_u32_le(&buffer[12], header->connection_id);
    xgl_serialize_u32_le(&buffer[16], header->packet_number);
    xgl_serialize_u16_le(&buffer[20], header->payload_len);
    xgl_serialize_u16_le(&buffer[22], wire_header_crc16(buffer));

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_header(xgl_wire_header_t* header,
                                   const uint8_t* buffer,
                                   size_t buffer_size) {
    if (header == NULL || buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (buffer[0] != XGL_WIRE_MAGIC_0 || buffer[1] != XGL_WIRE_MAGIC_1) {
        return XGL_ERR_INVALID_FRAME;
    }

    uint16_t expected_crc = wire_header_crc16(buffer);
    uint16_t actual_crc = xgl_deserialize_u16_le(&buffer[22]);
    if (expected_crc != actual_crc) {
        return XGL_ERR_CRC_FAILED;
    }

    memset(header, 0, sizeof(*header));
    header->version = buffer[2];
    header->header_len = buffer[3];
    header->packet_type = buffer[4];
    header->flags = buffer[5];
    header->ttl = buffer[6];
    header->traffic_class = buffer[7];
    header->source_id = xgl_deserialize_u16_le(&buffer[8]);
    header->target_id = xgl_deserialize_u16_le(&buffer[10]);
    header->connection_id = xgl_deserialize_u32_le(&buffer[12]);
    header->packet_number = xgl_deserialize_u32_le(&buffer[16]);
    header->payload_len = xgl_deserialize_u16_le(&buffer[20]);
    header->header_crc16 = actual_crc;

    if (header->version != XGL_WIRE_VERSION ||
        header->header_len < XGL_WIRE_BASE_HEADER_SIZE ||
        header->packet_type == XGL_PACKET_TYPE_INVALID ||
        header->source_id == 0U ||
        header->target_id == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}

xgl_error_t xgl_wire_encode_ext(uint8_t* buffer,
                                size_t buffer_size,
                                uint8_t type,
                                const uint8_t* value,
                                size_t value_len,
                                size_t* bytes_written) {
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (type == 0U || value_len > UINT8_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (value_len > 0U && value == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    size_t required_size = XGL_WIRE_EXT_HEADER_SIZE + value_len;
    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    buffer[0] = type;
    buffer[1] = (uint8_t)value_len;
    if (value_len > 0U) {
        memcpy(&buffer[2], value, value_len);
    }
    *bytes_written = required_size;

    return XGL_OK;
}

xgl_error_t xgl_wire_ext_cursor_init(xgl_wire_ext_cursor_t* cursor,
                                     const uint8_t* buffer,
                                     size_t len) {
    if (cursor == NULL || (buffer == NULL && len > 0U)) {
        return XGL_ERR_NULL_POINTER;
    }

    cursor->buffer = buffer;
    cursor->len = len;
    cursor->offset = 0;

    return XGL_OK;
}

xgl_error_t xgl_wire_ext_cursor_next(xgl_wire_ext_cursor_t* cursor,
                                     xgl_wire_ext_t* ext) {
    if (cursor == NULL || ext == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    memset(ext, 0, sizeof(*ext));

    if (cursor->offset == cursor->len) {
        return XGL_ERR_NOT_FOUND;
    }

    if (cursor->len - cursor->offset < XGL_WIRE_EXT_HEADER_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    uint8_t type = cursor->buffer[cursor->offset];
    uint8_t value_len = cursor->buffer[cursor->offset + 1U];
    size_t ext_len = XGL_WIRE_EXT_HEADER_SIZE + (size_t)value_len;
    if (type == 0U || ext_len > cursor->len - cursor->offset) {
        return XGL_ERR_INVALID_FRAME;
    }

    ext->type = type;
    ext->len = value_len;
    ext->value = &cursor->buffer[cursor->offset + XGL_WIRE_EXT_HEADER_SIZE];
    ext->valid = true;
    cursor->offset += ext_len;

    return XGL_OK;
}

xgl_error_t xgl_wire_encode_ack_range_ext_value(uint8_t* buffer,
                                                size_t buffer_size,
                                                uint32_t largest_ack,
                                                uint32_t ack_delay_us,
                                                const xgl_wire_ack_range_t* ranges,
                                                size_t range_count,
                                                size_t* bytes_written) {
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (range_count > 0U && ranges == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    size_t required_size = 9U + (range_count * 4U);
    if (required_size > UINT8_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_serialize_u32_le(&buffer[0], largest_ack);
    xgl_serialize_u32_le(&buffer[4], ack_delay_us);
    buffer[8] = (uint8_t)range_count;

    size_t offset = 9U;
    for (size_t i = 0; i < range_count; ++i) {
        xgl_serialize_u16_le(&buffer[offset], ranges[i].gap);
        xgl_serialize_u16_le(&buffer[offset + 2U], ranges[i].length);
        offset += 4U;
    }

    *bytes_written = required_size;
    return XGL_OK;
}

xgl_error_t xgl_wire_decode_ack_range_ext_value(const uint8_t* buffer,
                                                size_t buffer_size,
                                                uint32_t* largest_ack,
                                                uint32_t* ack_delay_us,
                                                xgl_wire_ack_range_t* ranges,
                                                size_t range_capacity,
                                                size_t* range_count) {
    if (buffer == NULL || largest_ack == NULL || ack_delay_us == NULL ||
        range_count == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < 9U) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t encoded_count = buffer[8];
    size_t required_size = 9U + (encoded_count * 4U);
    if (required_size != buffer_size) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (encoded_count > 0U && ranges == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (range_capacity < encoded_count) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    *largest_ack = xgl_deserialize_u32_le(&buffer[0]);
    *ack_delay_us = xgl_deserialize_u32_le(&buffer[4]);
    *range_count = encoded_count;

    size_t offset = 9U;
    for (size_t i = 0; i < encoded_count; ++i) {
        ranges[i].gap = xgl_deserialize_u16_le(&buffer[offset]);
        ranges[i].length = xgl_deserialize_u16_le(&buffer[offset + 2U]);
        offset += 4U;
    }

    return XGL_OK;
}

xgl_error_t xgl_wire_encode_sack_ext_value(uint8_t* buffer,
                                           size_t buffer_size,
                                           uint32_t base_packet,
                                           const uint8_t* bitmap,
                                           size_t bitmap_len,
                                           size_t* bytes_written) {
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (bitmap_len > 0U && bitmap == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    size_t required_size = 5U + bitmap_len;
    if (bitmap_len > UINT8_MAX || required_size > UINT8_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_serialize_u32_le(&buffer[0], base_packet);
    buffer[4] = (uint8_t)bitmap_len;
    if (bitmap_len > 0U) {
        memcpy(&buffer[5], bitmap, bitmap_len);
    }

    *bytes_written = required_size;
    return XGL_OK;
}

xgl_error_t xgl_wire_decode_sack_ext_value(const uint8_t* buffer,
                                           size_t buffer_size,
                                           uint32_t* base_packet,
                                           uint8_t* bitmap,
                                           size_t bitmap_capacity,
                                           size_t* bitmap_len) {
    if (buffer == NULL || base_packet == NULL || bitmap_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < 5U) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t encoded_bitmap_len = buffer[4];
    if ((5U + encoded_bitmap_len) != buffer_size) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (encoded_bitmap_len > 0U && bitmap == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (bitmap_capacity < encoded_bitmap_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    *base_packet = xgl_deserialize_u32_le(&buffer[0]);
    *bitmap_len = encoded_bitmap_len;
    if (encoded_bitmap_len > 0U) {
        memcpy(bitmap, &buffer[5], encoded_bitmap_len);
    }

    return XGL_OK;
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

    if (buffer_size < 12U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_serialize_u32_le(&buffer[0], message_id);
    xgl_serialize_u32_le(&buffer[4], fragment_offset);
    xgl_serialize_u32_le(&buffer[8], message_len);
    *bytes_written = 12U;

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

    if (buffer_size != 12U) {
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

    if (buffer_size < 12U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_serialize_u32_le(&buffer[0], session_epoch);
    wire_serialize_u64_le(&buffer[4], incarnation_id);
    *bytes_written = 12U;

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_session_ext_value(const uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t* session_epoch,
                                              uint64_t* incarnation_id) {
    if (buffer == NULL || session_epoch == NULL || incarnation_id == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != 12U) {
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

xgl_error_t xgl_wire_append_auth_trailer(uint8_t* buffer,
                                         size_t buffer_size,
                                         size_t aad_len,
                                         size_t payload_len,
                                         uint32_t key_id,
                                         const xgl_auth_provider_t* provider,
                                         size_t* frame_len) {
    if (buffer == NULL || provider == NULL || provider->sign == NULL ||
        frame_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (provider->tag_len == 0U || provider->tag_len > UINT8_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t tag_offset = aad_len + payload_len;
    if (tag_offset > buffer_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t tag_len = 0U;
    xgl_error_t err = provider->sign(key_id,
                                     buffer,
                                     aad_len,
                                     &buffer[aad_len],
                                     payload_len,
                                     &buffer[tag_offset],
                                     buffer_size - tag_offset,
                                     &tag_len,
                                     provider->user_data);
    if (err != XGL_OK) {
        return err;
    }

    if (tag_len != provider->tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (tag_len == 0U || tag_offset + tag_len > buffer_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    *frame_len = tag_offset + tag_len;
    return XGL_OK;
}

xgl_error_t xgl_wire_verify_auth_trailer(const uint8_t* buffer,
                                         size_t frame_len,
                                         size_t aad_len,
                                         size_t payload_len,
                                         uint32_t key_id,
                                         const xgl_auth_provider_t* provider,
                                         bool* valid) {
    if (buffer == NULL || provider == NULL || provider->verify == NULL ||
        valid == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (provider->tag_len == 0U || provider->tag_len > UINT8_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }

    *valid = false;
    size_t tag_offset = aad_len + payload_len;
    if (tag_offset >= frame_len) {
        return XGL_ERR_INVALID_FRAME;
    }
    if (frame_len - tag_offset != provider->tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }

    return provider->verify(key_id,
                            buffer,
                            aad_len,
                            &buffer[aad_len],
                            payload_len,
                            &buffer[tag_offset],
                            frame_len - tag_offset,
                            valid,
                            provider->user_data);
}
