/**
 * \file            xgl_frame.c
 * \brief           Frame encapsulation implementation
 * \author          X-Gen Lab
 */

#include <wire/xgl_frame.h>
#include <wire/xgl_frame_internal.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl_error.h>
#include <xgl/xgl_types.h>

#include <string.h>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

#define XGL_FRAME_DEFAULT_TTL 8U

/*---------------------------------------------------------------------------*/
/* Frame Header                                                              */
/*---------------------------------------------------------------------------*/

/** \brief           Normalize derived header fields from the checked layout. */
static xgl_error_t encode_frame_wire_header(uint8_t* buffer,
                                            const xgl_frame_t* frame,
                                            const xgl_frame_layout_t* layout) {
    uint8_t flags = frame->header.flags;
    if (layout->tag_len > 0U) {
        flags |= XGL_WIRE_FLAG_AUTHENTICATED;
    }
    if (layout->header_len > XGL_WIRE_BASE_HEADER_SIZE) {
        flags |= XGL_WIRE_FLAG_HAS_EXTENSIONS;
    }

    xgl_wire_header_t wire = frame->header;
    wire.version = XGL_WIRE_VERSION;
    wire.header_len = (uint8_t)layout->header_len;
    wire.flags = flags;
    wire.payload_len = (uint16_t)layout->payload_len;
    wire.header_crc16 = 0;

    return xgl_wire_encode_header(buffer, layout->frame_len, &wire);
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

    return XGL_OK;
}

/* Layout lengths follow payload, existing TLV, then appended tag order. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Validate lengths before any sum, write or allocation
 */
xgl_error_t xgl_frame_measure_lengths(size_t payload_len, size_t extensions_len,
                                      size_t auth_tag_len,
                                      xgl_frame_layout_t* layout) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (layout == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(layout, 0, sizeof(*layout));
    if (payload_len > UINT16_MAX ||
        extensions_len > UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if (auth_tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_PARAM;
    }
    size_t extension_len = extensions_len;
    if (auth_tag_len > 0U) {
        if (extension_len >
            UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE - XGL_SECURITY_EXT_SIZE) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        extension_len += XGL_SECURITY_EXT_SIZE;
    }
    const size_t header_len = XGL_WIRE_BASE_HEADER_SIZE + extension_len;
    const size_t overhead = header_len + auth_tag_len + XGL_CRC16_SIZE;
    if (payload_len > SIZE_MAX - overhead) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    layout->header_len = header_len;
    layout->payload_len = payload_len;
    layout->tag_len = auth_tag_len;
    layout->frame_len = overhead + payload_len;
    return XGL_OK;
}

xgl_error_t xgl_frame_measure(const xgl_frame_t* frame, size_t auth_tag_len,
                              xgl_frame_layout_t* layout) {
    if (layout == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(layout, 0, sizeof(*layout));
    if (frame == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_error_t error = xgl_frame_measure_lengths(
        frame->payload_len, frame->extensions_len, auth_tag_len, layout);
    if (error != XGL_OK) {
        return error;
    }
    if ((frame->payload_len > 0U && frame->payload == NULL) ||
        (frame->extensions_len > 0U && frame->extensions == NULL)) {
        memset(layout, 0, sizeof(*layout));
        return XGL_ERR_NULL_POINTER;
    }
    if (auth_tag_len > 0U) {
        xgl_wire_ext_metadata_t metadata;
        error = xgl_wire_decode_ext_metadata(frame->extensions,
                                             frame->extensions_len, &metadata);
        if (error != XGL_OK || metadata.has_security_ext) {
            memset(layout, 0, sizeof(*layout));
            return XGL_ERR_INVALID_FRAME;
        }
    }
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
    return xgl_frame_encode_measured(buffer, frame, layout);
}

xgl_error_t xgl_frame_encode_measured(uint8_t* buffer, const xgl_frame_t* frame,
                                      const xgl_frame_layout_t* layout) {
    xgl_error_t error = encode_frame_wire_header(buffer, frame, layout);
    if (error != XGL_OK) {
        return error;
    }
    if (frame->extensions_len > 0U) {
        memmove(buffer + XGL_WIRE_BASE_HEADER_SIZE, frame->extensions,
                frame->extensions_len);
    }
    if (layout->tag_len > 0U) {
        uint8_t* extension =
            buffer + XGL_WIRE_BASE_HEADER_SIZE + frame->extensions_len;
        extension[0] = XGL_WIRE_EXT_SECURITY;
        extension[1] = XGL_SECURITY_EXT_VALUE_SIZE;
        size_t written = 0U;
        error = xgl_wire_encode_security_ext_value(
            extension + XGL_WIRE_EXT_HEADER_SIZE, XGL_SECURITY_EXT_VALUE_SIZE,
            0U, 0U, (uint8_t)layout->tag_len, &written);
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

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_frame_build_zerocopy(uint8_t* buffer, size_t buffer_size,
                                     size_t data_offset, size_t data_len,
                                     uint16_t source_id, uint16_t target_id,
                                     uint8_t data_type, uint32_t packet_number,
                                     bool reliable, uint8_t priority,
                                     size_t* frame_len) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || frame_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (data_len > UINT16_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t app_type_ext_len = (data_type != 0U) ? XGL_DATA_TYPE_EXT_SIZE : 0U;
    xgl_frame_layout_t layout;
    xgl_error_t error =
        xgl_frame_measure_lengths(data_len, app_type_ext_len, 0U, &layout);
    if (error != XGL_OK) {
        return error;
    }
    if (data_offset != layout.header_len) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (buffer_size < layout.frame_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t extensions[XGL_DATA_TYPE_EXT_SIZE];
    if (app_type_ext_len > 0U) {
        size_t written = 0U;
        error = xgl_wire_encode_ext(extensions, sizeof(extensions),
                                    XGL_WIRE_EXT_DATA_TYPE, &data_type, 1U,
                                    &written);
        if (error != XGL_OK) {
            return error;
        }
    }
    xgl_frame_params_t params = {.source_id = source_id,
                                 .target_id = target_id,
                                 .packet_number = packet_number,
                                 .extensions =
                                     app_type_ext_len > 0U ? extensions : NULL,
                                 .extensions_len = app_type_ext_len,
                                 .payload = buffer + data_offset,
                                 .payload_len = data_len,
                                 .reliable = reliable,
                                 .priority = priority,
                                 .ttl = XGL_FRAME_DEFAULT_TTL};
    xgl_frame_t frame;
    error = xgl_frame_build(&frame, &params);
    if (error != XGL_OK) {
        return error;
    }
    return xgl_frame_serialize(buffer, buffer_size, &frame, frame_len);
}
