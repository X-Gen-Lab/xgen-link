/**
 * \file            xgl_wire.c
 * \brief           Production wire-format encoding primitives
 * \author          X-Gen Lab
 * \version         5.0.0
 * \date            2026-11-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <wire/xgl_crc16.h>
#include <wire/xgl_wire.h>
#include <wire/xgl_wire_bytes.h>
#include <wire/xgl_wire_rx_internal.h>
#include <xgl/xgl_config.h>

#include <string.h>

#define WIRE_HEADER_CRC_OFFSET (XGL_WIRE_BASE_HEADER_SIZE - XGL_CRC16_SIZE)

static uint16_t wire_finish_header_crc(uint16_t prefix_crc) {
    const uint8_t zero_crc[XGL_CRC16_SIZE] = {0U, 0U};
    return xgl_crc16_modbus_update(prefix_crc, zero_crc, sizeof(zero_crc));
}

static uint16_t wire_header_crc16(const uint8_t* buffer) {
    return wire_finish_header_crc(
        xgl_crc16_modbus(buffer, WIRE_HEADER_CRC_OFFSET));
}

void xgl_wire_decrement_frame_ttl(uint8_t* buffer, size_t frame_len) {
    --buffer[XGL_WIRE_TTL_OFFSET];
    const uint16_t prefix_crc =
        xgl_crc16_modbus(buffer, WIRE_HEADER_CRC_OFFSET);
    xgl_serialize_u16_le(buffer + WIRE_HEADER_CRC_OFFSET,
                         wire_finish_header_crc(prefix_crc));

    /* The frame CRC continues over the stored header CRC and remaining body. */
    const size_t crc_offset = frame_len - XGL_CRC16_SIZE;
    const uint16_t frame_crc =
        xgl_crc16_modbus_update(prefix_crc, buffer + WIRE_HEADER_CRC_OFFSET,
                                crc_offset - WIRE_HEADER_CRC_OFFSET);
    xgl_serialize_u16_le(buffer + crc_offset, frame_crc);
}

static bool wire_header_valid(const xgl_wire_header_t* header) {
    const uint8_t known_flags =
        XGL_WIRE_FLAG_ACK_ELICITING | XGL_WIRE_FLAG_HAS_EXTENSIONS |
        XGL_WIRE_FLAG_AUTHENTICATED | XGL_WIRE_FLAG_CONTROL;
    const uint8_t known_traffic =
        XGL_RELIABILITY_CLASS_MASK | XGL_TRAFFIC_PRIORITY_MASK;
    const bool has_extensions =
        (header->flags & XGL_WIRE_FLAG_HAS_EXTENSIONS) != 0U;
    if (header->version != XGL_WIRE_VERSION ||
        header->header_len < XGL_WIRE_BASE_HEADER_SIZE ||
        (header->flags & (uint8_t)~known_flags) != 0U ||
        (header->traffic_class & (uint8_t)~known_traffic) != 0U ||
        has_extensions != (header->header_len > XGL_WIRE_BASE_HEADER_SIZE)) {
        return false;
    }

    const uint8_t reliability =
        (uint8_t)(header->traffic_class & XGL_RELIABILITY_CLASS_MASK);
    const bool ack_eliciting =
        (header->flags & XGL_WIRE_FLAG_ACK_ELICITING) != 0U;
    const bool control = (header->flags & XGL_WIRE_FLAG_CONTROL) != 0U;
    if (header->packet_type == XGL_PACKET_TYPE_DATA) {
        return !control &&
               (ack_eliciting ? reliability == XGL_RELIABILITY_ACK_ELICITING
                              : reliability == XGL_RELIABILITY_NONE &&
                                    header->packet_number == 0U);
    }

    if (ack_eliciting || header->packet_number != 0U ||
        header->payload_len != 0U) {
        return false;
    }
    if (header->packet_type == XGL_PACKET_TYPE_ACK) {
        return !control && reliability == XGL_RELIABILITY_ACK_ONLY;
    }
    return header->packet_type == XGL_PACKET_TYPE_CONTROL && control &&
           reliability == XGL_RELIABILITY_NONE;
}

/** \brief           Load fixed fields from a complete header byte span. */
static void wire_load_header(xgl_wire_header_t* header, const uint8_t* buffer) {
    header->version = buffer[2];
    header->header_len = buffer[3];
    header->packet_type = buffer[4];
    header->flags = buffer[5];
    header->ttl = buffer[XGL_WIRE_TTL_OFFSET];
    header->traffic_class = buffer[7];
    header->source_id = xgl_deserialize_u16_le(buffer + 8U);
    header->target_id = xgl_deserialize_u16_le(buffer + 10U);
    header->connection_id = xgl_deserialize_u32_le(buffer + 12U);
    header->packet_number = xgl_deserialize_u32_le(buffer + 16U);
    header->payload_len = xgl_deserialize_u16_le(buffer + 20U);
    header->header_crc16 =
        xgl_deserialize_u16_le(buffer + WIRE_HEADER_CRC_OFFSET);
}

xgl_error_t xgl_wire_encode_header(uint8_t* buffer, size_t buffer_size,
                                   const xgl_wire_header_t* header) {
    if (buffer == NULL || header == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (!wire_header_valid(header)) {
        return XGL_ERR_INVALID_PARAM;
    }

    /* Every byte is assigned; header CRC uses its own zeroed CRC suffix. */
    buffer[0] = XGL_WIRE_MAGIC_0;
    buffer[1] = XGL_WIRE_MAGIC_1;
    buffer[2] = header->version;
    buffer[3] = header->header_len;
    buffer[4] = header->packet_type;
    buffer[5] = header->flags;
    buffer[XGL_WIRE_TTL_OFFSET] = header->ttl;
    buffer[7] = header->traffic_class;
    xgl_serialize_u16_le(&buffer[8], header->source_id);
    xgl_serialize_u16_le(&buffer[10], header->target_id);
    xgl_serialize_u32_le(&buffer[12], header->connection_id);
    xgl_serialize_u32_le(&buffer[16], header->packet_number);
    xgl_serialize_u16_le(&buffer[20], header->payload_len);
    xgl_serialize_u16_le(buffer + WIRE_HEADER_CRC_OFFSET,
                         wire_header_crc16(buffer));

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_header(xgl_wire_header_t* header,
                                   const uint8_t* buffer, size_t buffer_size) {
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
    uint16_t actual_crc =
        xgl_deserialize_u16_le(buffer + WIRE_HEADER_CRC_OFFSET);
    if (expected_crc != actual_crc) {
        return XGL_ERR_CRC_FAILED;
    }

    wire_load_header(header, buffer);

    if (!wire_header_valid(header)) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}

xgl_error_t xgl_wire_rx_check_header(xgl_wire_header_t* header,
                                     const uint8_t* buffer, size_t available,
                                     xgl_wire_decode_status_t* status) {
    const xgl_error_t error = xgl_wire_decode_header(header, buffer, available);
    if (status != NULL && error == XGL_ERR_CRC_FAILED) {
        *status = XGL_WIRE_DECODE_HEADER_CRC;
    }
    return error;
}

xgl_error_t xgl_wire_rx_check_extensions(xgl_wire_rx_layout_t* layout,
                                         const uint8_t* buffer,
                                         uint8_t header_len) {
    xgl_wire_ext_metadata_t extensions;
    const bool authenticated = (buffer[5] & XGL_WIRE_FLAG_AUTHENTICATED) != 0U;
    const xgl_error_t error = xgl_wire_decode_ext_metadata(
        buffer + XGL_WIRE_BASE_HEADER_SIZE,
        (size_t)header_len - XGL_WIRE_BASE_HEADER_SIZE, authenticated,
        &extensions);
    if (error != XGL_OK) {
        return error;
    }

    *layout = (xgl_wire_rx_layout_t){
        .data_type_offset = extensions.data_type_offset,
        .session_offset = extensions.session_offset,
        .security_offset = extensions.security_offset,
        .auth_tag_len = extensions.auth_tag_len,
    };
    return XGL_OK;
}

/** \brief           Materialize fields at validated offsets without a scan. */
static void wire_load_extensions(xgl_wire_frame_view_t* view,
                                 const xgl_wire_rx_layout_t* layout) {
    const uint8_t* extensions = view->extensions;
    if (layout->data_type_offset != 0U) {
        view->data_type = extensions[layout->data_type_offset];
    }
    if (layout->session_offset != 0U) {
        const uint8_t* session = extensions + layout->session_offset;
        view->session_epoch = xgl_deserialize_u32_le(session);
        view->incarnation_id = xgl_deserialize_u64_le(session + 4U);
    }
    if (layout->security_offset != 0U) {
        const uint8_t* security = extensions + layout->security_offset;
        view->auth_key_id = xgl_deserialize_u32_le(security);
        view->nonce_id = xgl_deserialize_u64_le(security + 4U);
    }
}

xgl_error_t xgl_wire_rx_complete(xgl_wire_frame_view_t* view,
                                 const uint8_t* buffer, size_t frame_len,
                                 const xgl_wire_rx_layout_t* layout,
                                 xgl_wire_decode_status_t* status) {
    if (frame_len < XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }
    const uint8_t header_len = buffer[3];
    const uint16_t payload_len = xgl_deserialize_u16_le(buffer + 20U);
    const size_t crc_offset = frame_len - XGL_CRC16_SIZE;
    if (header_len > crc_offset) {
        return XGL_ERR_INVALID_FRAME;
    }
    const size_t body_len = crc_offset - header_len;
    if (payload_len > body_len ||
        body_len - payload_len != layout->auth_tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }
    if (xgl_crc16_modbus(buffer, crc_offset) !=
        xgl_deserialize_u16_le(buffer + crc_offset)) {
        if (status != NULL) {
            *status = XGL_WIRE_DECODE_FRAME_CRC;
        }
        return XGL_ERR_CRC_FAILED;
    }

    memset(view, 0, sizeof(*view));
    wire_load_header(&view->header, buffer);
    view->extensions_len = (size_t)header_len - XGL_WIRE_BASE_HEADER_SIZE;
    if (view->extensions_len != 0U) {
        view->extensions = buffer + XGL_WIRE_BASE_HEADER_SIZE;
    }
    view->payload = buffer + header_len;
    view->payload_len = payload_len;
    view->reliable = (uint8_t)(buffer[7] & XGL_RELIABILITY_CLASS_MASK);
    view->priority = (uint8_t)((buffer[7] & XGL_TRAFFIC_PRIORITY_MASK) >>
                               XGL_TRAFFIC_PRIORITY_SHIFT);
    view->frame_buf = buffer;
    view->frame_len = frame_len;
    view->auth_tag_len = layout->auth_tag_len;
    view->has_security_ext = layout->security_offset != 0U;
    view->authenticated = view->has_security_ext;
    wire_load_extensions(view, layout);
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_OK;
    }
    return XGL_OK;
}

xgl_error_t xgl_wire_decode_frame(xgl_wire_frame_view_t* view,
                                  const uint8_t* buffer, size_t frame_len,
                                  xgl_wire_decode_status_t* status) {
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_INVALID;
    }
    if (view == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_error_t error;
    if (buffer == NULL) {
        error = XGL_ERR_NULL_POINTER;
        goto failed;
    }
    if (frame_len < XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE) {
        error = XGL_ERR_INVALID_FRAME;
        goto failed;
    }
    error = xgl_wire_rx_check_header(&view->header, buffer, frame_len, status);
    if (error != XGL_OK) {
        goto failed;
    }

    /* Establish the actual extension boundary before reading any TLV byte. */
    const uint8_t header_len = view->header.header_len;
    if (header_len > frame_len - XGL_CRC16_SIZE) {
        error = XGL_ERR_INVALID_FRAME;
        goto failed;
    }
    xgl_wire_rx_layout_t layout;
    error = xgl_wire_rx_check_extensions(&layout, buffer, header_len);
    if (error == XGL_OK) {
        error = xgl_wire_rx_complete(view, buffer, frame_len, &layout, status);
    }
    if (error == XGL_OK) {
        return XGL_OK;
    }

failed:
    memset(view, 0, sizeof(*view));
    return error;
}
