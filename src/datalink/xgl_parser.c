/**
 * \file            xgl_parser.c
 * \brief           Frame parser state machine implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_parser.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_crc.h>
#include <xgl/xgl_serialize.h>
#include <xgl/xgl_wire.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Parser Initialization                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize frame parser
 */
xgl_error_t xgl_parser_init(xgl_parser_t* parser,
                            uint8_t* cache_buffer,
                            size_t cache_size) {
    if (parser == NULL || cache_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (cache_size < (XGL_FRAME_HEADER_SIZE + XGL_CRC16_SIZE)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Initialize parser structure */
    memset(parser, 0, sizeof(xgl_parser_t));
    parser->state = XGL_PARSE_SOF;
    parser->cache = cache_buffer;
    parser->cache_size = cache_size;
    parser->cache_len = 0;
    parser->index = 0;
    parser->timestamp = 0;
    parser->expected_header_len = 0;
    parser->expected_payload_len = 0;
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Parser Reset                                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Reset parser to initial state
 */
void xgl_parser_reset(xgl_parser_t* parser) {
    if (parser == NULL) {
        return;
    }
    
    parser->state = XGL_PARSE_SOF;
    parser->cache_len = 0;
    parser->index = 0;
    parser->timestamp = 0;
    parser->expected_header_len = 0;
    parser->expected_payload_len = 0;
}

static xgl_parse_result_t validate_extensions_or_reset(xgl_parser_t* parser) {
    size_t ext_len = parser->expected_header_len - XGL_WIRE_BASE_HEADER_SIZE;
    if (ext_len == 0U) {
        return XGL_PARSE_RESULT_INCOMPLETE;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(&cursor,
                                               &parser->cache[XGL_WIRE_BASE_HEADER_SIZE],
                                               ext_len);
    if (err != XGL_OK) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        /* Extension cursor performs structural TLV validation. */
    }

    if (err != XGL_ERR_NOT_FOUND) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
    }

    return XGL_PARSE_RESULT_INCOMPLETE;
}

/*---------------------------------------------------------------------------*/
/* Parser State Machine                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Feed byte to parser
 * \details         Implements byte-by-byte parsing state machine
 */
xgl_parse_result_t xgl_parser_feed_byte(xgl_parser_t* parser,
                                        uint8_t byte,
                                        uint32_t current_time_ms) {
    if (parser == NULL) {
        return XGL_PARSE_RESULT_ERROR;
    }
    
    /* Check for buffer overflow */
    if (parser->cache_len >= parser->cache_size) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
    }
    
    switch (parser->state) {
        /*-------------------------------------------------------------------*/
        /* State: Searching for SOF                                         */
        /*-------------------------------------------------------------------*/
        case XGL_PARSE_SOF:
            if (byte == XGL_WIRE_MAGIC_0) {
                /* Found SOF, store it and move to header state */
                parser->cache[0] = byte;
                parser->cache_len = 1;
                parser->index = 0;
                parser->timestamp = current_time_ms;
                parser->state = XGL_PARSE_HEADER;
            }
            /* Ignore all other bytes while searching for SOF */
            return XGL_PARSE_RESULT_INCOMPLETE;
        
        /*-------------------------------------------------------------------*/
        /* State: Receiving frame header                                    */
        /*-------------------------------------------------------------------*/
        case XGL_PARSE_HEADER:
            /* Store header byte */
            parser->cache[parser->cache_len++] = byte;
            
            /* Check if we have complete production base header. */
            if (parser->cache_len >= XGL_WIRE_BASE_HEADER_SIZE) {
                xgl_wire_header_t header;
                if (xgl_wire_decode_header(&header,
                                           parser->cache,
                                           XGL_WIRE_BASE_HEADER_SIZE) != XGL_OK) {
                    /* Header validation failed, reset and search for next magic */
                    xgl_parser_reset(parser);
                    return XGL_PARSE_RESULT_ERROR;
                }
                
                parser->expected_header_len = header.header_len;
                parser->expected_payload_len = header.payload_len;

                /* Check if payload fits in cache */
                size_t total_frame_size = parser->expected_header_len +
                                         parser->expected_payload_len + 
                                         XGL_CRC16_SIZE;
                if (total_frame_size > parser->cache_size) {
                    /* Frame too large for cache buffer */
                    xgl_parser_reset(parser);
                    return XGL_PARSE_RESULT_ERROR;
                }

                if (parser->cache_len < parser->expected_header_len) {
                    return XGL_PARSE_RESULT_INCOMPLETE;
                }

                xgl_parse_result_t ext_result = validate_extensions_or_reset(parser);
                if (ext_result == XGL_PARSE_RESULT_ERROR) {
                    return ext_result;
                }
                
                /* Move to payload state (or CRC if no payload) */
                if (parser->expected_payload_len > 0) {
                    parser->state = XGL_PARSE_PAYLOAD;
                } else {
                    parser->state = XGL_PARSE_CRC;
                }
            }
            return XGL_PARSE_RESULT_INCOMPLETE;
        
        /*-------------------------------------------------------------------*/
        /* State: Receiving payload data                                    */
        /*-------------------------------------------------------------------*/
        case XGL_PARSE_PAYLOAD:
            /* Store payload byte */
            parser->cache[parser->cache_len++] = byte;
            
            /* Check if we have complete payload */
            size_t payload_received = parser->cache_len - parser->expected_header_len;
            if (payload_received >= parser->expected_payload_len) {
                /* Move to CRC state */
                parser->state = XGL_PARSE_CRC;
                parser->index = 0;
            }
            return XGL_PARSE_RESULT_INCOMPLETE;
        
        /*-------------------------------------------------------------------*/
        /* State: Receiving CRC16                                           */
        /*-------------------------------------------------------------------*/
        case XGL_PARSE_CRC:
            /* Store CRC byte */
            parser->cache[parser->cache_len++] = byte;
            parser->index++;
            
            /* Check if we have complete CRC16 (2 bytes) */
            if (parser->index >= XGL_CRC16_SIZE) {
                /* Calculate expected CRC16 (all data except CRC16 itself) */
                size_t crc_offset = parser->cache_len - XGL_CRC16_SIZE;
                uint16_t calculated_crc = xgl_crc16_modbus(parser->cache, crc_offset);
                
                /* Extract received CRC16 */
                uint16_t received_crc = xgl_deserialize_u16_le(&parser->cache[crc_offset]);
                
                /* Validate CRC16 */
                if (calculated_crc != received_crc) {
                    /* CRC16 validation failed */
                    xgl_parser_reset(parser);
                    return XGL_PARSE_RESULT_ERROR;
                }
                
                /* Frame complete and valid */
                return XGL_PARSE_RESULT_COMPLETE;
            }
            return XGL_PARSE_RESULT_INCOMPLETE;
        
        default:
            /* Invalid state, reset parser */
            xgl_parser_reset(parser);
            return XGL_PARSE_RESULT_ERROR;
    }
}

/*---------------------------------------------------------------------------*/
/* Parser Timeout Handling                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Check for parser timeout
 */
bool xgl_parser_check_timeout(const xgl_parser_t* parser,
                              uint32_t current_time_ms,
                              uint32_t timeout_ms) {
    if (parser == NULL) {
        return false;
    }
    
    /* No timeout if parser is idle (waiting for SOF) */
    if (parser->state == XGL_PARSE_SOF) {
        return false;
    }
    
    /* Check if timeout occurred */
    uint32_t elapsed = current_time_ms - parser->timestamp;
    return elapsed >= timeout_ms;
}

/*---------------------------------------------------------------------------*/
/* Parser Data Retrieval                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get parsed frame data
 */
xgl_error_t xgl_parser_get_frame(const xgl_parser_t* parser,
                                 uint8_t** frame_buffer,
                                 size_t* frame_len) {
    if (parser == NULL || frame_buffer == NULL || frame_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check if frame is complete */
    if (parser->state != XGL_PARSE_CRC || parser->cache_len == 0) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    *frame_buffer = parser->cache;
    *frame_len = parser->cache_len;
    
    return XGL_OK;
}
