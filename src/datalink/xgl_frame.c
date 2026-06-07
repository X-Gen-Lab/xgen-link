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

#define XGL_AUTH_TRAILER_TAG_CAPACITY 32U

/*---------------------------------------------------------------------------*/
/* Frame Header Encoding/Decoding                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Encode frame header to buffer
 * \note            Header already contains SOF, so we encode the entire structure
 */
void xgl_frame_encode_header(uint8_t* buffer, const xgl_legacy_frame_header_t* header) {
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

static xgl_error_t encode_frame_wire_header(uint8_t* buffer,
                                            size_t buffer_size,
                                            const xgl_frame_t* frame,
                                            size_t extension_len,
                                            uint8_t extra_flags,
                                            size_t* header_len) {
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
 * \brief           Decode frame header from buffer
 */
void xgl_frame_decode_header(xgl_legacy_frame_header_t* header, const uint8_t* buffer) {
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
    header->source_id = wire.source_id;
    header->target_id = wire.target_id;
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
    
    uint8_t attr_lsb = 0;
    if (params->reliable_type != XGL_ATTR_RELIABLE_NONE) {
        xgl_frame_set_reliable_type(&attr_lsb, params->reliable_type);
    } else {
        xgl_frame_set_reliable(&attr_lsb, params->reliable);
    }
    xgl_frame_set_fragment(&attr_lsb, params->fragment);
    xgl_frame_set_priority(&attr_lsb, params->priority);

    uint8_t flags = params->flags;
    uint8_t reliable = (uint8_t)(attr_lsb & XGL_ATTR_RELIABLE_MASK);
    if (reliable == XGL_ATTR_RELIABLE_TX) {
        flags |= XGL_WIRE_FLAG_ACK_ELICITING;
    } else if (reliable == XGL_ATTR_RELIABLE_ACK) {
        flags |= XGL_WIRE_FLAG_CONTROL;
    }
    if ((attr_lsb & XGL_ATTR_FRAGMENT_MASK) != 0U) {
        flags |= XGL_WIRE_FLAG_FRAGMENTED | XGL_WIRE_FLAG_HAS_EXTENSIONS;
    }

    uint8_t packet_type = params->packet_type;
    if (packet_type == XGL_PACKET_TYPE_INVALID) {
        packet_type = params->data_type;
    }
    if (packet_type == XGL_PACKET_TYPE_INVALID) {
        packet_type = XGL_PACKET_TYPE_DATA;
    }
    if (reliable == XGL_ATTR_RELIABLE_ACK) {
        packet_type = XGL_PACKET_TYPE_ACK;
    }

    frame->header.version = XGL_WIRE_VERSION;
    frame->header.header_len = XGL_WIRE_BASE_HEADER_SIZE;
    frame->header.packet_type = packet_type;
    frame->header.flags = flags;
    frame->header.ttl = params->ttl;
    frame->header.traffic_class = (params->traffic_class != 0U) ?
                                  params->traffic_class :
                                  (uint8_t)(attr_lsb & XGL_ATTR_PRIORITY_MASK);
    frame->header.source_id = params->source_id;
    frame->header.target_id = params->target_id;
    frame->header.connection_id = (params->connection_id != 0U) ?
                                  params->connection_id :
                                  (uint32_t)params->session_id;
    frame->header.packet_number = (params->packet_number != 0U) ?
                                  params->packet_number :
                                  (uint32_t)params->seq_num;
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
    if (frame->extensions_len > UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t required_size = XGL_FRAME_HEADER_SIZE + frame->extensions_len +
                           frame->payload_len + XGL_CRC16_SIZE;
    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    size_t offset = 0;
    
    size_t header_len = 0;
    xgl_error_t err = encode_frame_wire_header(buffer,
                                               buffer_size,
                                               frame,
                                               frame->extensions_len,
                                               0U,
                                               &header_len);
    if (err != XGL_OK) {
        return err;
    }
    offset += header_len;

    if (frame->extensions != NULL && frame->extensions_len > 0U) {
        memcpy(&buffer[XGL_WIRE_BASE_HEADER_SIZE],
               frame->extensions,
               frame->extensions_len);
    }
    
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

static xgl_error_t encode_authenticated_header(uint8_t* buffer,
                                               size_t buffer_size,
                                               const xgl_frame_t* frame,
                                               uint32_t key_id,
                                               uint8_t tag_len,
                                               size_t* header_len) {
    size_t base_ext_len = frame->extensions_len;
    if (base_ext_len > UINT8_MAX - XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if (buffer_size < XGL_WIRE_BASE_HEADER_SIZE + base_ext_len +
                      XGL_WIRE_EXT_HEADER_SIZE + 13U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    if (frame->extensions != NULL && base_ext_len > 0U) {
        memcpy(&buffer[XGL_WIRE_BASE_HEADER_SIZE],
               frame->extensions,
               base_ext_len);
    }

    uint8_t security_value[13] = {0};
    size_t security_value_len = 0;
    xgl_error_t err = xgl_wire_encode_security_ext_value(security_value,
                                                         sizeof(security_value),
                                                         key_id,
                                                         frame->header.packet_number,
                                                         tag_len,
                                                         &security_value_len);
    if (err != XGL_OK) {
        return err;
    }

    size_t security_ext_len = 0;
    err = xgl_wire_encode_ext(&buffer[XGL_WIRE_BASE_HEADER_SIZE + base_ext_len],
                              buffer_size - XGL_WIRE_BASE_HEADER_SIZE - base_ext_len,
                              XGL_WIRE_EXT_SECURITY,
                              security_value,
                              security_value_len,
                              &security_ext_len);
    if (err != XGL_OK) {
        return err;
    }

    size_t produced_header_len = XGL_WIRE_BASE_HEADER_SIZE + base_ext_len + security_ext_len;
    if (produced_header_len > UINT8_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgl_wire_header_t wire = frame->header;
    wire.version = XGL_WIRE_VERSION;
    wire.header_len = (uint8_t)produced_header_len;
    wire.flags = (uint8_t)(wire.flags |
                           XGL_WIRE_FLAG_HAS_EXTENSIONS |
                           XGL_WIRE_FLAG_AUTHENTICATED);
    wire.payload_len = (uint16_t)frame->payload_len;
    wire.header_crc16 = 0;

    err = xgl_wire_encode_header(buffer, buffer_size, &wire);
    if (err != XGL_OK) {
        return err;
    }

    *header_len = produced_header_len;
    return XGL_OK;
}

xgl_error_t xgl_frame_serialize_authenticated(uint8_t* buffer,
                                              size_t buffer_size,
                                              const xgl_frame_t* frame,
                                              uint32_t key_id,
                                              const xgl_auth_provider_t* provider,
                                              size_t* bytes_written) {
    if (buffer == NULL || frame == NULL || provider == NULL ||
        provider->sign == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (frame->payload_len > UINT16_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t header_len = 0;
    xgl_error_t err = encode_authenticated_header(buffer,
                                                  buffer_size,
                                                  frame,
                                                  key_id,
                                                  0U,
                                                  &header_len);
    if (err != XGL_OK) {
        return err;
    }

    if (buffer_size < header_len + frame->payload_len + XGL_CRC16_SIZE + 1U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (frame->payload != NULL && frame->payload_len > 0U) {
        memcpy(&buffer[header_len], frame->payload, frame->payload_len);
    }

    uint8_t trial_tag[XGL_AUTH_TRAILER_TAG_CAPACITY] = {0};
    size_t tag_len = 0;
    err = provider->sign(key_id,
                         buffer,
                         header_len,
                         &buffer[header_len],
                         frame->payload_len,
                         trial_tag,
                         sizeof(trial_tag),
                         &tag_len,
                         provider->user_data);
    if (err != XGL_OK) {
        return err;
    }
    if (tag_len == 0U || tag_len > UINT8_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    err = encode_authenticated_header(buffer,
                                      buffer_size,
                                      frame,
                                      key_id,
                                      (uint8_t)tag_len,
                                      &header_len);
    if (err != XGL_OK) {
        return err;
    }
    if (frame->payload != NULL && frame->payload_len > 0U) {
        memcpy(&buffer[header_len], frame->payload, frame->payload_len);
    }

    size_t frame_len_without_crc = 0;
    err = xgl_wire_append_auth_trailer(buffer,
                                       buffer_size - XGL_CRC16_SIZE,
                                       header_len,
                                       frame->payload_len,
                                       key_id,
                                       provider,
                                       &frame_len_without_crc);
    if (err != XGL_OK) {
        return err;
    }
    if (frame_len_without_crc + XGL_CRC16_SIZE > buffer_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint16_t crc16 = xgl_crc16_modbus(buffer, frame_len_without_crc);
    xgl_serialize_u16_le(&buffer[frame_len_without_crc], crc16);
    *bytes_written = frame_len_without_crc + XGL_CRC16_SIZE;
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
    xgl_legacy_frame_header_t header;
    memset(&header, 0, sizeof(header));
    header.sof = XGL_SOF;
    xgl_frame_set_version(&header, XGL_PROTOCOL_VERSION);
    xgl_frame_set_datatype(&header, data_type);
    header.source_id = source_id;
    header.target_id = target_id;
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
bool xgl_frame_validate_header_crc(const xgl_legacy_frame_header_t* header) {
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
