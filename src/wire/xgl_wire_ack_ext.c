/**
 * \file            xgl_wire_ack_ext.c
 * \brief           Wire-format ACK extension value codecs
 */

#include <xgl/internal/xgl_wire.h>

#include <string.h>
#include <xgen/bytes/bytes.h>

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

    xgb_serialize_u32_le(&buffer[0], largest_ack);
    xgb_serialize_u32_le(&buffer[4], ack_delay_us);
    buffer[8] = (uint8_t)range_count;

    size_t offset = 9U;
    for (size_t i = 0; i < range_count; ++i) {
        xgb_serialize_u16_le(&buffer[offset], ranges[i].gap);
        xgb_serialize_u16_le(&buffer[offset + 2U], ranges[i].length);
        offset += 4U;
    }

    *bytes_written = required_size;
    return XGL_OK;
}

xgl_error_t xgl_wire_decode_ack_range_ext_value(
    const uint8_t* buffer, size_t buffer_size, uint32_t* largest_ack,
    uint32_t* ack_delay_us, xgl_wire_ack_range_t* ranges, size_t range_capacity,
    size_t* range_count) {
    if (buffer == NULL || largest_ack == NULL || ack_delay_us == NULL ||
        range_count == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_wire_ack_range_view_t view;
    xgl_error_t error =
        xgl_wire_decode_ack_range_view(buffer, buffer_size, &view);
    if (error != XGL_OK) {
        return error;
    }

    if (view.range_count > 0U && ranges == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (range_capacity < view.range_count) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    *largest_ack = view.largest_ack;
    *ack_delay_us = view.ack_delay_us;
    *range_count = view.range_count;
    for (size_t i = 0U; i < view.range_count; ++i) {
        (void)xgl_wire_ack_range_at(&view, i, &ranges[i]);
    }

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
    view->largest_ack = xgb_deserialize_u32_le(buffer);
    view->ack_delay_us = xgb_deserialize_u32_le(buffer + 4U);
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
    range->gap = xgb_deserialize_u16_le(bytes);
    range->length = xgb_deserialize_u16_le(bytes + 2U);
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

    size_t required_size = 5U + bitmap_len;
    if (bitmap_len > UINT8_MAX || required_size > UINT8_MAX) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgb_serialize_u32_le(&buffer[0], base_packet);
    buffer[4] = (uint8_t)bitmap_len;
    if (bitmap_len > 0U) {
        memcpy(&buffer[5], bitmap, bitmap_len);
    }

    *bytes_written = required_size;
    return XGL_OK;
}

xgl_error_t
xgl_wire_decode_sack_ext_value(const uint8_t* buffer, size_t buffer_size,
                               uint32_t* base_packet, uint8_t* bitmap,
                               size_t bitmap_capacity, size_t* bitmap_len) {
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

    *base_packet = xgb_deserialize_u32_le(&buffer[0]);
    *bitmap_len = encoded_bitmap_len;
    if (encoded_bitmap_len > 0U) {
        memcpy(bitmap, &buffer[5], encoded_bitmap_len);
    }

    return XGL_OK;
}
