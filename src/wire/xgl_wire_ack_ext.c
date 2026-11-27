/**
 * \file            xgl_wire_ack_ext.c
 * \brief           Wire-format ACK extension value codecs
 * \author          X-Gen Lab
 * \version         5.0.0
 * \date            2026-11-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <wire/xgl_wire.h>
#include <wire/xgl_wire_bytes.h>

#include <string.h>

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t
xgl_wire_encode_ack_range_ext_value(uint8_t* buffer, size_t buffer_size,
                                    uint32_t largest_ack, uint32_t ack_delay_us,
                                    const xgl_wire_ack_range_t* ranges,
                                    size_t range_count, size_t* bytes_written) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (range_count > 0U && ranges == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (range_count > (UINT8_MAX - 9U) / 4U) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t required_size = 9U + (range_count * 4U);

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

xgl_error_t xgl_wire_decode_ack_range_view(const uint8_t* buffer,
                                           size_t buffer_size,
                                           xgl_wire_ack_range_view_t* view) {
    if (view == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(view, 0, sizeof(*view));
    if (buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (buffer_size < 9U || 9U + (size_t)buffer[8] * 4U != buffer_size) {
        return XGL_ERR_INVALID_FRAME;
    }
    view->largest_ack = xgl_deserialize_u32_le(buffer);
    view->ack_delay_us = xgl_deserialize_u32_le(buffer + 4U);
    view->range_count = buffer[8];
    view->ranges = buffer + 9U;
    return XGL_OK;
}

xgl_error_t xgl_wire_ack_range_at(const xgl_wire_ack_range_view_t* view,
                                  size_t index, xgl_wire_ack_range_t* range) {
    if (view == NULL || range == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (index >= view->range_count) {
        return XGL_ERR_NOT_FOUND;
    }
    if (view->ranges == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    const uint8_t* bytes = view->ranges + index * 4U;
    range->gap = xgl_deserialize_u16_le(bytes);
    range->length = xgl_deserialize_u16_le(bytes + 2U);
    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_wire_encode_sack_ext_value(uint8_t* buffer, size_t buffer_size,
                                           uint32_t base_packet,
                                           const uint8_t* bitmap,
                                           size_t bitmap_len,
                                           size_t* bytes_written) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (bitmap_len > 0U && bitmap == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (bitmap_len > UINT8_MAX - 5U) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t required_size = 5U + bitmap_len;

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

xgl_error_t xgl_wire_decode_sack_view(const uint8_t* buffer, size_t buffer_size,
                                      xgl_wire_sack_view_t* view) {
    if (view == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(view, 0, sizeof(*view));
    if (buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (buffer_size < 5U || 5U + (size_t)buffer[4] != buffer_size) {
        return XGL_ERR_INVALID_FRAME;
    }
    view->base_packet = xgl_deserialize_u32_le(buffer);
    view->bitmap = buffer + 5U;
    view->bitmap_len = buffer[4];
    return XGL_OK;
}
