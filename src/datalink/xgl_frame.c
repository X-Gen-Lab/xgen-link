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
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Protocol Version                                                          */
/*---------------------------------------------------------------------------*/

#define XGL_PROTOCOL_VERSION    0x01

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
    
    /* Copy the packed structure directly */
    /* Note: data_len needs to be in little-endian format */
    buffer[0] = header->sof;
    buffer[1] = header->version_datatype;
    buffer[2] = header->source_id;
    buffer[3] = header->target_id;
    buffer[4] = header->attr_lsb;
    buffer[5] = header->attr_msb;
    xgl_serialize_u16_le(&buffer[6], header->data_len);
    buffer[8] = header->seq_num;
    buffer[9] = header->ack_num;
    buffer[10] = header->reserved;
    buffer[11] = header->crc8;
}

/**
 * \brief           Decode frame header from buffer
 */
void xgl_frame_decode_header(xgl_frame_header_t* header, const uint8_t* buffer) {
    if (header == NULL || buffer == NULL) {
        return;
    }
    
    header->sof = buffer[0];
    header->version_datatype = buffer[1];
    header->source_id = buffer[2];
    header->target_id = buffer[3];
    header->attr_lsb = buffer[4];
    header->attr_msb = buffer[5];
    header->data_len = xgl_deserialize_u16_le(&buffer[6]);
    header->seq_num = buffer[8];
    header->ack_num = buffer[9];
    header->reserved = buffer[10];
    header->crc8 = buffer[11];
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
    
    /* Initialize frame */
    memset(frame, 0, sizeof(xgl_frame_t));
    
    /* Build header */
    frame->header.sof = XGL_SOF;
    xgl_frame_set_version(&frame->header, XGL_PROTOCOL_VERSION);
    xgl_frame_set_datatype(&frame->header, params->data_type);
    frame->header.source_id = params->source_id;
    frame->header.target_id = params->target_id;
    frame->header.data_len = (uint16_t)params->payload_len;
    frame->header.seq_num = params->seq_num;
    frame->header.ack_num = params->ack_num;
    frame->header.reserved = 0;
    
    /* Set attributes */
    frame->header.attr_lsb = 0;
    frame->header.attr_msb = 0;
    if (params->reliable_type != XGL_ATTR_RELIABLE_NONE) {
        xgl_frame_set_reliable_type(&frame->header.attr_lsb, params->reliable_type);
    } else {
        xgl_frame_set_reliable(&frame->header.attr_lsb, params->reliable);
    }
    xgl_frame_set_fragment(&frame->header.attr_lsb, params->fragment);
    xgl_frame_set_priority(&frame->header.attr_lsb, params->priority);
    
    /* Calculate header CRC8 (first 11 bytes of header, excluding CRC8 itself) */
    uint8_t header_buf[XGL_FRAME_HEADER_SIZE];
    xgl_frame_encode_header(header_buf, &frame->header);
    frame->header.crc8 = xgl_crc8_maxim(header_buf, 11);
    
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
                                     uint8_t source_id,
                                     uint8_t target_id,
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
    header.source_id = source_id;
    header.target_id = target_id;
    header.data_len = (uint16_t)data_len;
    header.seq_num = seq_num;
    header.ack_num = ack_num;
    header.reserved = 0;
    
    /* Set attributes */
    header.attr_lsb = 0;
    header.attr_msb = 0;
    xgl_frame_set_reliable(&header.attr_lsb, reliable);
    xgl_frame_set_priority(&header.attr_lsb, priority);
    
    /* Encode header to buffer */
    xgl_frame_encode_header(&buffer[header_offset], &header);
    
    /* Calculate and write header CRC8 */
    buffer[header_offset + 11] = xgl_crc8_maxim(&buffer[header_offset], 11);
    
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
    
    /* Encode header to buffer */
    uint8_t header_buf[XGL_FRAME_HEADER_SIZE];
    xgl_frame_encode_header(header_buf, header);
    
    /* Calculate CRC8 on first 11 bytes (all except CRC8 itself) */
    uint8_t calculated_crc = xgl_crc8_maxim(header_buf, 11);
    
    /* Compare with stored CRC8 */
    return calculated_crc == header->crc8;
}
