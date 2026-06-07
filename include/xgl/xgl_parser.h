/**
 * \file            xgl_parser.h
 * \brief           Frame parser state machine
 * \author          Nexus Team
 */

#ifndef XGL_PARSER_H
#define XGL_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_types.h"
#include "xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Parser State Machine                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Parser state enumeration
 */
typedef enum {
    XGL_PARSE_SOF,              /**< Searching for start of frame */
    XGL_PARSE_HEADER,           /**< Receiving frame header */
    XGL_PARSE_PAYLOAD,          /**< Receiving payload data */
    XGL_PARSE_CRC,              /**< Receiving CRC16 */
} xgl_parse_state_t;

/**
 * \brief           Parser result enumeration
 */
typedef enum {
    XGL_PARSE_RESULT_INCOMPLETE,    /**< Frame incomplete, need more data */
    XGL_PARSE_RESULT_COMPLETE,      /**< Frame complete and valid */
    XGL_PARSE_RESULT_ERROR,         /**< Parse error occurred */
} xgl_parse_result_t;

/**
 * \brief           Frame parser structure
 */
typedef struct {
    xgl_parse_state_t state;    /**< Current parser state */
    uint8_t* cache;             /**< Cache buffer for frame data */
    size_t cache_size;          /**< Total cache buffer size */
    size_t cache_len;           /**< Current data length in cache */
    size_t index;               /**< Current parsing index */
    uint32_t timestamp;         /**< Timestamp when parsing started (ms) */
    size_t expected_header_len; /**< Expected fixed + extension header length */
    uint16_t expected_payload_len; /**< Expected payload length from header */
    uint8_t expected_auth_tag_len; /**< Expected authentication trailer length */
} xgl_parser_t;

/*---------------------------------------------------------------------------*/
/* Parser Configuration                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default parser timeout in milliseconds
 */
#define XGL_PARSER_TIMEOUT_MS   1000

/*---------------------------------------------------------------------------*/
/* Parser Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize frame parser
 * \param[out]      parser: Parser structure to initialize
 * \param[in]       cache_buffer: Cache buffer for frame data
 * \param[in]       cache_size: Size of cache buffer
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_parser_init(xgl_parser_t* parser,
                            uint8_t* cache_buffer,
                            size_t cache_size);

/**
 * \brief           Reset parser to initial state
 * \param[in,out]   parser: Parser structure to reset
 */
void xgl_parser_reset(xgl_parser_t* parser);

/**
 * \brief           Feed byte to parser
 * \param[in,out]   parser: Parser structure
 * \param[in]       byte: Byte to parse
 * \param[in]       current_time_ms: Current time in milliseconds
 * \return          Parse result (incomplete, complete, or error)
 */
xgl_parse_result_t xgl_parser_feed_byte(xgl_parser_t* parser,
                                        uint8_t byte,
                                        uint32_t current_time_ms);

/**
 * \brief           Check for parser timeout
 * \param[in]       parser: Parser structure
 * \param[in]       current_time_ms: Current time in milliseconds
 * \param[in]       timeout_ms: Timeout value in milliseconds
 * \return          true if timeout occurred, false otherwise
 */
bool xgl_parser_check_timeout(const xgl_parser_t* parser,
                              uint32_t current_time_ms,
                              uint32_t timeout_ms);

/**
 * \brief           Get parsed frame data
 * \param[in]       parser: Parser structure
 * \param[out]      frame_buffer: Output buffer for complete frame
 * \param[out]      frame_len: Length of complete frame
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_parser_get_frame(const xgl_parser_t* parser,
                                 uint8_t** frame_buffer,
                                 size_t* frame_len);

/**
 * \brief           Get parser state (for debugging)
 * \param[in]       parser: Parser structure
 * \return          Current parser state
 */
static inline xgl_parse_state_t xgl_parser_get_state(const xgl_parser_t* parser) {
    return parser ? parser->state : XGL_PARSE_SOF;
}

/**
 * \brief           Get cached data length (for debugging)
 * \param[in]       parser: Parser structure
 * \return          Current cached data length
 */
static inline size_t xgl_parser_get_cached_len(const xgl_parser_t* parser) {
    return parser ? parser->cache_len : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_PARSER_H */
