/**
 * \file            xgl_wire_ack_ext.c
 * \brief           Wire-format ACK extension value codecs
 */

#include <xgl/internal/xgl_serialize.h>
#include <xgl/internal/xgl_wire.h>
#include <string.h>

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
