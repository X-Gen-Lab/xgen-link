/**
 * \file            xgl_frame.c
 * \brief           Frame encapsulation implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_wire.h>
#include <xgl/xgl_error.h>
#include <xgl/xgl_types.h>

#include <string.h>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

/*---------------------------------------------------------------------------*/
/* Protocol Version                                                          */
/*---------------------------------------------------------------------------*/

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
static xgl_error_t encode_frame_wire_header(uint8_t* buffer, size_t buffer_size,
                                            const xgl_frame_t* frame,
                                            size_t extension_len,
                                            uint8_t extra_flags,
                                            size_t* header_len) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || frame == NULL || header_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (extension_len > UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t produced_header_len = XGL_WIRE_BASE_HEADER_SIZE + extension_len;
    uint8_t flags = (uint8_t)(frame->header.flags | extra_flags);
    if (extension_len > 0U) {
        flags |= XGL_WIRE_FLAG_HAS_EXTENSIONS;
    }

    xgl_wire_header_t wire = frame->header;
    wire.version = XGL_WIRE_VERSION;
    wire.header_len = (uint8_t)produced_header_len;
    wire.flags = flags;
    wire.payload_len = (uint16_t)frame->payload_len;
    wire.header_crc16 = 0;

    xgl_error_t err = xgl_wire_encode_header(buffer, buffer_size, &wire);
    if (err != XGL_OK) {
        return err;
    }
    *header_len = produced_header_len;
    return XGL_OK;
}

/**
 * \brief           Build frame from parameters
 */
xgl_error_t xgl_frame_build(xgl_frame_t* frame,
                            const xgl_frame_params_t* params) {
    if (frame == NULL || params == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (params->payload_len > UINT16_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    /* Initialize frame */
    memset(frame, 0, sizeof(xgl_frame_t));

    uint8_t traffic_class_bits = 0;
    if (params->reliability_class != XGL_RELIABILITY_NONE) {
        xgl_frame_set_reliability_class(&traffic_class_bits,
                                        params->reliability_class);
    } else {
        xgl_frame_set_reliability(&traffic_class_bits, params->reliable);
    }
    xgl_frame_set_fragmented(&traffic_class_bits, params->fragment);
    xgl_frame_set_priority(&traffic_class_bits, params->priority);

    uint8_t flags = params->flags;
    uint8_t reliable =
        (uint8_t)(traffic_class_bits & XGL_RELIABILITY_CLASS_MASK);
    if (reliable == XGL_RELIABILITY_ACK_ELICITING) {
        flags |= XGL_WIRE_FLAG_ACK_ELICITING;
    }
    if ((traffic_class_bits & XGL_TRAFFIC_FRAGMENTED_MASK) != 0U) {
        flags |= XGL_WIRE_FLAG_FRAGMENTED | XGL_WIRE_FLAG_HAS_EXTENSIONS;
    }
    if (params->extensions != NULL && params->extensions_len > 0U) {
        flags |= XGL_WIRE_FLAG_HAS_EXTENSIONS;
    }

    uint8_t packet_type = params->packet_type;
    if (packet_type == XGL_PACKET_TYPE_INVALID) {
        packet_type = XGL_PACKET_TYPE_DATA;
    }
    if (reliable == XGL_RELIABILITY_ACK_ONLY) {
        packet_type = XGL_PACKET_TYPE_ACK;
    }
    if (packet_type == XGL_PACKET_TYPE_CONTROL) {
        flags |= XGL_WIRE_FLAG_CONTROL;
    }

    frame->header.version = XGL_WIRE_VERSION;
    frame->header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    frame->header.packet_type = packet_type;
    frame->header.flags = flags;
    frame->header.ttl = params->ttl;
    frame->header.traffic_class = (params->traffic_class != 0U)
                                      ? params->traffic_class
                                      : traffic_class_bits;
    frame->header.source_id = params->source_id;
    frame->header.target_id = params->target_id;
    frame->header.connection_id = params->connection_id;
    frame->header.packet_number =
        packet_type == XGL_PACKET_TYPE_DATA &&
                (reliable == XGL_RELIABILITY_ACK_ELICITING ||
                 (flags & XGL_WIRE_FLAG_ACK_ELICITING) != 0U)
            ? params->packet_number
            : 0U;
    frame->header.payload_len = (uint16_t)params->payload_len;
    frame->header.header_crc16 = 0;

    /* Set payload */
    frame->payload = params->payload;
    frame->payload_len = params->payload_len;
    frame->extensions = params->extensions;
    frame->extensions_len = params->extensions_len;

    /* CRC16 will be calculated during serialization */
    frame->crc16 = 0;

    return XGL_OK;
}

/**
 * \brief           Validate lengths before any sum, write or allocation
 */
xgl_error_t xgl_frame_measure(const xgl_frame_t* frame, size_t auth_tag_len,
                              xgl_frame_layout_t* layout) {
    if (layout == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(layout, 0, sizeof(*layout));
    if (frame == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (frame->payload_len > UINT16_MAX ||
        frame->extensions_len > UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if ((frame->payload_len > 0U && frame->payload == NULL) ||
        (frame->extensions_len > 0U && frame->extensions == NULL)) {
        return XGL_ERR_NULL_POINTER;
    }
    if (auth_tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_PARAM;
    }
    size_t extension_len = frame->extensions_len;
    if (auth_tag_len > 0U) {
        if (extension_len >
            UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE - XGL_SECURITY_EXT_SIZE) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        xgl_wire_ext_metadata_t metadata;
        xgl_error_t error = xgl_wire_decode_ext_metadata(
            frame->extensions, extension_len, &metadata);
        if (error != XGL_OK || metadata.has_security_ext) {
            return XGL_ERR_INVALID_FRAME;
        }
        extension_len += XGL_SECURITY_EXT_SIZE;
    }
    const size_t header_len = XGL_WIRE_BASE_HEADER_SIZE + extension_len;
    const size_t overhead = header_len + auth_tag_len + XGL_CRC16_SIZE;
    if (frame->payload_len > SIZE_MAX - overhead) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    layout->header_len = header_len;
    layout->payload_len = frame->payload_len;
    layout->tag_len = auth_tag_len;
    layout->frame_len = overhead + frame->payload_len;
    return XGL_OK;
}

xgl_error_t xgl_frame_encode_into(uint8_t* buffer, size_t buffer_size,
                                  const xgl_frame_t* frame, size_t auth_tag_len,
                                  xgl_frame_layout_t* layout) {
    if (buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_error_t error = xgl_frame_measure(frame, auth_tag_len, layout);
    if (error != XGL_OK) {
        return error;
    }
    if (buffer_size < layout->frame_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    size_t encoded_header_len = 0U;
    error = encode_frame_wire_header(
        buffer, buffer_size, frame,
        layout->header_len - XGL_WIRE_BASE_HEADER_SIZE,
        auth_tag_len > 0U ? XGL_WIRE_FLAG_AUTHENTICATED : 0U,
        &encoded_header_len);
    if (error != XGL_OK) {
        return error;
    }
    if (frame->extensions_len > 0U) {
        memmove(buffer + XGL_WIRE_BASE_HEADER_SIZE, frame->extensions,
                frame->extensions_len);
    }
    if (auth_tag_len > 0U) {
        uint8_t* extension =
            buffer + XGL_WIRE_BASE_HEADER_SIZE + frame->extensions_len;
        extension[0] = XGL_WIRE_EXT_SECURITY;
        extension[1] = XGL_SECURITY_EXT_VALUE_SIZE;
        size_t written = 0U;
        error = xgl_wire_encode_security_ext_value(
            extension + XGL_WIRE_EXT_HEADER_SIZE, XGL_SECURITY_EXT_VALUE_SIZE,
            0U, 0U, (uint8_t)auth_tag_len, &written);
        if (error != XGL_OK) {
            return error;
        }
    }
    uint8_t* payload = buffer + layout->header_len;
    if (frame->payload_len > 0U && frame->payload != payload) {
        memmove(payload, frame->payload, frame->payload_len);
    }
    return XGL_OK;
}

xgl_error_t xgl_frame_finalize_crc(uint8_t* buffer, size_t buffer_size,
                                   const xgl_frame_layout_t* layout,
                                   size_t* bytes_written) {
    if (bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *bytes_written = 0U;
    if (buffer == NULL || layout == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (layout->frame_len < XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE ||
        layout->frame_len > buffer_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    size_t crc_offset = layout->frame_len - XGL_CRC16_SIZE;
    xgb_serialize_u16_le(buffer + crc_offset,
                         xgcrc_crc16_modbus(buffer, crc_offset));
    *bytes_written = layout->frame_len;
    return XGL_OK;
}

xgl_error_t xgl_frame_serialize(uint8_t* buffer, size_t buffer_size,
                                const xgl_frame_t* frame,
                                size_t* bytes_written) {
    if (bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *bytes_written = 0U;
    xgl_frame_layout_t layout;
    xgl_error_t error =
        xgl_frame_encode_into(buffer, buffer_size, frame, 0U, &layout);
    if (error != XGL_OK) {
        return error;
    }
    return xgl_frame_finalize_crc(buffer, buffer_size, &layout, bytes_written);
}
