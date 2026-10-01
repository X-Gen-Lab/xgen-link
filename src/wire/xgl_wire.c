/**
 * \file            xgl_wire.c
 * \brief           Production wire-format encoding primitives
 */

#include <wire/xgl_wire.h>
#include <xgl/xgl_config.h>

#include <string.h>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

static uint16_t wire_header_crc16(const uint8_t* buffer) {
    uint8_t crc_input[XGL_WIRE_BASE_HEADER_SIZE];
    memcpy(crc_input, buffer, XGL_WIRE_BASE_HEADER_SIZE);
    crc_input[22] = 0;
    crc_input[23] = 0;
    return xgcrc_crc16_modbus(crc_input, XGL_WIRE_BASE_HEADER_SIZE);
}

static bool wire_packet_type_valid(uint8_t packet_type) {
    return packet_type > XGL_PACKET_TYPE_INVALID &&
           packet_type <= XGL_PACKET_TYPE_CLOSE;
}

xgl_error_t xgl_wire_encode_header(uint8_t* buffer, size_t buffer_size,
                                   const xgl_wire_header_t* header) {
    if (buffer == NULL || header == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (header->version != XGL_WIRE_VERSION ||
        header->header_len < XGL_WIRE_BASE_HEADER_SIZE ||
        !wire_packet_type_valid(header->packet_type) ||
        (header->flags & XGL_WIRE_FLAG_ENCRYPTED) != 0U ||
        (header->traffic_class & XGL_TRAFFIC_ENCRYPTION_MASK) != 0U ||
        header->source_id == 0U || header->target_id == 0U) {
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
    xgb_serialize_u16_le(&buffer[8], header->source_id);
    xgb_serialize_u16_le(&buffer[10], header->target_id);
    xgb_serialize_u32_le(&buffer[12], header->connection_id);
    xgb_serialize_u32_le(&buffer[16], header->packet_number);
    xgb_serialize_u16_le(&buffer[20], header->payload_len);
    xgb_serialize_u16_le(&buffer[22], wire_header_crc16(buffer));

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
    uint16_t actual_crc = xgb_deserialize_u16_le(&buffer[22]);
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
    header->source_id = xgb_deserialize_u16_le(&buffer[8]);
    header->target_id = xgb_deserialize_u16_le(&buffer[10]);
    header->connection_id = xgb_deserialize_u32_le(&buffer[12]);
    header->packet_number = xgb_deserialize_u32_le(&buffer[16]);
    header->payload_len = xgb_deserialize_u16_le(&buffer[20]);
    header->header_crc16 = actual_crc;

    if (header->version != XGL_WIRE_VERSION ||
        header->header_len < XGL_WIRE_BASE_HEADER_SIZE ||
        !wire_packet_type_valid(header->packet_type) ||
        (header->flags & XGL_WIRE_FLAG_ENCRYPTED) != 0U ||
        (header->traffic_class & XGL_TRAFFIC_ENCRYPTION_MASK) != 0U ||
        header->source_id == 0U || header->target_id == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}

/**
 * \brief           Validate frame boundaries and CRCs before exposing byte
 *                  spans
 * \param[out]      view: Borrowed view, cleared if decoding fails
 * \param[in]       buffer: Complete frame bytes
 * \param[in]       frame_len: Available bytes including the final CRC
 * \param[out]      status: Optional detailed validation result
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_wire_decode_frame(xgl_wire_frame_view_t* view,
                                  const uint8_t* buffer, size_t frame_len,
                                  xgl_wire_decode_status_t* status) {
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_INVALID;
    }
    if (view == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(view, 0, sizeof(*view));
    if (buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (frame_len < XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }
    xgl_wire_header_t header;
    xgl_error_t err = xgl_wire_decode_header(&header, buffer, frame_len);
    if (err != XGL_OK) {
        if (status != NULL && err == XGL_ERR_CRC_FAILED) {
            *status = XGL_WIRE_DECODE_HEADER_CRC;
        }
        return err;
    }

    /* Establish the actual extension boundary before reading any TLV byte. */
    size_t body_end = frame_len - XGL_CRC16_SIZE;
    if (header.header_len > body_end) {
        return XGL_ERR_INVALID_FRAME;
    }
    size_t extensions_len =
        (size_t)header.header_len - XGL_WIRE_BASE_HEADER_SIZE;
    xgl_wire_ext_metadata_t extensions;
    err = xgl_wire_decode_ext_metadata(buffer + XGL_WIRE_BASE_HEADER_SIZE,
                                       extensions_len, &extensions);
    if (err != XGL_OK) {
        return err;
    }
    size_t body_len = body_end - header.header_len;
    if (header.payload_len > body_len ||
        body_len - header.payload_len != extensions.auth_tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }
    bool authenticated = (header.flags & XGL_WIRE_FLAG_AUTHENTICATED) != 0U;
    if (authenticated != extensions.has_security_ext) {
        return XGL_ERR_INVALID_FRAME;
    }
    if (xgcrc_crc16_modbus(buffer, body_end) !=
        xgb_deserialize_u16_le(buffer + body_end)) {
        if (status != NULL) {
            *status = XGL_WIRE_DECODE_FRAME_CRC;
        }
        return XGL_ERR_CRC_FAILED;
    }

    view->header = header;
    view->data_type = extensions.data_type;
    view->session_epoch = extensions.session_epoch;
    view->extensions =
        extensions_len > 0U ? buffer + XGL_WIRE_BASE_HEADER_SIZE : NULL;
    view->extensions_len = extensions_len;
    view->payload = buffer + header.header_len;
    view->payload_len = header.payload_len;
    view->frame_buf = buffer;
    view->frame_len = frame_len;
    view->auth_key_id = extensions.auth_key_id;
    view->nonce_id = extensions.nonce_id;
    view->incarnation_id = extensions.incarnation_id;
    view->auth_tag_len = extensions.auth_tag_len;
    view->has_security_ext = extensions.has_security_ext;
    view->authenticated = authenticated;
    uint8_t reliability =
        (uint8_t)(header.traffic_class & XGL_RELIABILITY_CLASS_MASK);
    if (reliability == XGL_RELIABILITY_ACK_ELICITING ||
        (header.flags & XGL_WIRE_FLAG_ACK_ELICITING) != 0U) {
        view->reliable = XGL_RELIABILITY_ACK_ELICITING;
    } else if (reliability == XGL_RELIABILITY_ACK_ONLY ||
               header.packet_type == XGL_PACKET_TYPE_ACK) {
        view->reliable = XGL_RELIABILITY_ACK_ONLY;
    }
    view->fragment = (header.flags & XGL_WIRE_FLAG_FRAGMENTED) != 0U;
    view->priority =
        (uint8_t)((header.traffic_class & XGL_TRAFFIC_PRIORITY_MASK) >>
                  XGL_TRAFFIC_PRIORITY_SHIFT);
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_OK;
    }
    return XGL_OK;
}
