/**
 * \file            xgl_frame.c
 * \brief           Frame encapsulation implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_frame.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_types.h>
#include <xgl/xgl_error.h>
#include <xgl/xgl_wire.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Protocol Version                                                          */
/*---------------------------------------------------------------------------*/

#define XGL_PROTOCOL_VERSION    0x01
#define XGL_FRAME_DEFAULT_TTL   8U

/*---------------------------------------------------------------------------*/
/* Frame Header Encoding/Decoding                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Encode frame header to buffer
 * \note            Header already contains SOF, so we encode the entire structure
 */
void xgl_frame_encode_header(uint8_t* buffer, const xgl_frame_header_t* header) {
    if (buffer == NULL || header == NULL) {
        return;
    }

    uint8_t flags = 0;
    uint8_t reliable = (uint8_t)(header->attr_lsb & XGL_ATTR_RELIABLE_MASK);
    if (reliable == XGL_ATTR_RELIABLE_TX) {
        flags |= XGL_WIRE_FLAG_ACK_ELICITING;
    } else if (reliable == XGL_ATTR_RELIABLE_ACK) {
        flags |= XGL_WIRE_FLAG_CONTROL;
    }
    if ((header->attr_lsb & XGL_ATTR_FRAGMENT_MASK) != 0U) {
        flags |= XGL_WIRE_FLAG_FRAGMENTED | XGL_WIRE_FLAG_HAS_EXTENSIONS;
    }

    uint8_t packet_type = xgl_frame_get_datatype(header);
    if (packet_type == XGL_PACKET_TYPE_INVALID) {
        packet_type = XGL_PACKET_TYPE_DATA;
    }

    xgl_wire_header_t wire = {
        .version = XGL_WIRE_VERSION,
        .header_len = XGL_WIRE_BASE_HEADER_SIZE,
        .packet_type = (reliable == XGL_ATTR_RELIABLE_ACK) ?
                       XGL_PACKET_TYPE_ACK : packet_type,
        .flags = flags,
        .ttl = header->reserved,
        .traffic_class = (uint8_t)(header->attr_lsb & XGL_ATTR_PRIORITY_MASK),
        .source_id = header->source_id,
        .target_id = header->target_id,
        .connection_id = (uint32_t)(header->attr_msb & XGL_ATTR_SESSION_MASK),
        .packet_number = header->seq_num,
        .payload_len = header->data_len,
        .header_crc16 = 0
    };

    (void)xgl_wire_encode_header(buffer, XGL_WIRE_BASE_HEADER_SIZE, &wire);
}

/**
 * \brief           Decode frame header from buffer
 */
void xgl_frame_decode_header(xgl_frame_header_t* header, const uint8_t* buffer) {
    if (header == NULL || buffer == NULL) {
        return;
    }

    xgl_wire_header_t wire;
    memset(&wire, 0, sizeof(wire));
    if (xgl_wire_decode_header(&wire, buffer, XGL_WIRE_BASE_HEADER_SIZE) != XGL_OK) {
        memset(header, 0, sizeof(*header));
        return;
    }

    header->sof = XGL_SOF;
    header->version_datatype = 0;
    xgl_frame_set_version(header, XGL_PROTOCOL_VERSION);
    xgl_frame_set_datatype(header, wire.packet_type);
    header->source_id = (uint8_t)(wire.source_id & 0xFFU);
    header->target_id = (uint8_t)(wire.target_id & 0xFFU);
    header->attr_lsb = (uint8_t)(wire.traffic_class & XGL_ATTR_PRIORITY_MASK);
    if ((wire.flags & XGL_WIRE_FLAG_ACK_ELICITING) != 0U) {
        header->attr_lsb |= XGL_ATTR_RELIABLE_TX;
    } else if (wire.packet_type == XGL_PACKET_TYPE_ACK ||
               (wire.flags & XGL_WIRE_FLAG_CONTROL) != 0U) {
        header->attr_lsb |= XGL_ATTR_RELIABLE_ACK;
    }
    if ((wire.flags & XGL_WIRE_FLAG_FRAGMENTED) != 0U) {
        header->attr_lsb |= XGL_ATTR_FRAGMENT_MASK;
    }
    header->attr_msb = (uint8_t)(wire.connection_id & XGL_ATTR_SESSION_MASK);
    header->data_len = wire.payload_len;
    header->seq_num = (uint8_t)(wire.packet_number & 0xFFU);
    header->ack_num = 0;
    header->reserved = wire.ttl;
    header->crc8 = (uint8_t)(wire.header_crc16 & 0xFFU);
}

/*---------------------------------------------------------------------------*/
/* Frame Building                                                            */
/*---------------------------------------------------------------------------*/

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
    
    /* Build header */
    frame->header.sof = XGL_SOF;
    xgl_frame_set_version(&frame->header, XGL_PROTOCOL_VERSION);
    xgl_frame_set_datatype(&frame->header, params->data_type);
    frame->header.source_id = (uint8_t)(params->source_id & 0xFFU);
    frame->header.target_id = (uint8_t)(params->target_id & 0xFFU);
    frame->header.data_len = (uint16_t)params->payload_len;
    frame->header.seq_num = params->seq_num;
    frame->header.ack_num = params->ack_num;
    frame->header.reserved = params->ttl;
    
    /* Set attributes */
    frame->header.attr_lsb = 0;
    frame->header.attr_msb = (uint8_t)(params->session_id & XGL_ATTR_SESSION_MASK);
    if (params->reliable_type != XGL_ATTR_RELIABLE_NONE) {
        xgl_frame_set_reliable_type(&frame->header.attr_lsb, params->reliable_type);
    } else {
        xgl_frame_set_reliable(&frame->header.attr_lsb, params->reliable);
    }
    xgl_frame_set_fragment(&frame->header.attr_lsb, params->fragment);
    xgl_frame_set_priority(&frame->header.attr_lsb, params->priority);
    
    /* Calculate production header CRC through xgl_wire_encode_header. */
    uint8_t header_buf[XGL_FRAME_HEADER_SIZE];
    xgl_frame_encode_header(header_buf, &frame->header);
    frame->header.crc8 = header_buf[22];
    
    /* Set payload */
    frame->payload = params->payload;
    frame->payload_len = params->payload_len;
    
    /* CRC16 will be calculated during serialization */
    frame->crc16 = 0;
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Frame Serialization                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Serialize frame to buffer
 */
xgl_error_t xgl_frame_serialize(uint8_t* buffer,
                                size_t buffer_size,
                                const xgl_frame_t* frame,
                                size_t* bytes_written) {
    if (buffer == NULL || frame == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Calculate required buffer size */
    size_t required_size = xgl_frame_calculate_size(frame->payload_len);
    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    size_t offset = 0;
    
    /* Write header (includes SOF) */
    xgl_frame_encode_header(buffer, &frame->header);
    offset += XGL_FRAME_HEADER_SIZE;
    
    /* Write payload */
    if (frame->payload != NULL && frame->payload_len > 0) {
        memcpy(&buffer[offset], frame->payload, frame->payload_len);
        offset += frame->payload_len;
    }
    
    /* Calculate and write CRC16 (entire frame except CRC16 itself) */
    uint16_t crc16 = xgl_crc16_modbus(buffer, offset);
    xgl_serialize_u16_le(&buffer[offset], crc16);
    offset += XGL_CRC16_SIZE;
    
    *bytes_written = offset;
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Zero-Copy Frame Building                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Build frame in zero-copy mode
 */
xgl_error_t xgl_frame_build_zerocopy(uint8_t* buffer,
                                     size_t buffer_size,
                                     size_t data_offset,
                                     size_t data_len,
                                     uint16_t source_id,
                                     uint16_t target_id,
                                     uint8_t data_type,
                                     uint8_t seq_num,
                                     uint8_t ack_num,
                                     bool reliable,
                                     uint8_t priority,
                                     size_t* frame_len) {
    if (buffer == NULL || frame_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Validate buffer layout */
    if (data_offset < XGL_FRAME_HEADER_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    size_t required_size = data_offset + data_len + XGL_CRC16_SIZE;
    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Calculate header position (before data) */
    size_t header_offset = data_offset - XGL_FRAME_HEADER_SIZE;
    
    /* Build header structure */
    xgl_frame_header_t header;
    memset(&header, 0, sizeof(header));
    header.sof = XGL_SOF;
    xgl_frame_set_version(&header, XGL_PROTOCOL_VERSION);
    xgl_frame_set_datatype(&header, data_type);
    header.source_id = (uint8_t)(source_id & 0xFFU);
    header.target_id = (uint8_t)(target_id & 0xFFU);
    header.data_len = (uint16_t)data_len;
    header.seq_num = seq_num;
    header.ack_num = ack_num;
    header.reserved = XGL_FRAME_DEFAULT_TTL;
    
    /* Set attributes */
    header.attr_lsb = 0;
    header.attr_msb = 0;
    xgl_frame_set_reliable(&header.attr_lsb, reliable);
    xgl_frame_set_priority(&header.attr_lsb, priority);
    
    /* Encode header to buffer */
    xgl_frame_encode_header(&buffer[header_offset], &header);
    
    /* Header CRC is generated by xgl_frame_encode_header. */
    
    /* Calculate and write frame CRC16 */
    size_t crc_offset = data_offset + data_len;
    uint16_t crc16 = xgl_crc16_modbus(&buffer[header_offset], crc_offset - header_offset);
    xgl_serialize_u16_le(&buffer[crc_offset], crc16);
    
    *frame_len = required_size;
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Frame Validation                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Validate frame header CRC8
 */
bool xgl_frame_validate_header_crc(const xgl_frame_header_t* header) {
    if (header == NULL) {
        return false;
    }

    if (header->sof != XGL_SOF ||
        header->source_id == 0U ||
        header->target_id == 0U) {
        return false;
    }

    return true;
}
