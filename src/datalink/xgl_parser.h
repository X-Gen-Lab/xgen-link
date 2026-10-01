/**
 * \file            xgl_parser.h
 * \brief           Frame parser state machine
 * \author          X-Gen Lab
 */

#ifndef XGL_PARSER_H
#define XGL_PARSER_H

#include "wire/xgl_wire.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Parser State Machine                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Parser state enumeration
 */
typedef enum {
    XGL_PARSE_MAGIC,      /**< Searching for production magic */
    XGL_PARSE_HEADER,     /**< Receiving frame header */
    XGL_PARSE_PAYLOAD,    /**< Receiving payload data */
    XGL_PARSE_CRC,        /**< Receiving CRC16 */
    XGL_PARSE_EXTENSIONS, /**< Receiving the bounded header extensions */
    XGL_PARSE_COMPLETE,   /**< Complete frame passed the wire validator */
    XGL_PARSE_RECOVER,    /**< Rejected candidate awaiting buffered recovery */
} xgl_parse_state_t;

/**
 * \brief           Parser result enumeration
 */
typedef enum {
    XGL_PARSE_RESULT_INCOMPLETE, /**< Frame incomplete, need more data */
    XGL_PARSE_RESULT_COMPLETE,   /**< Frame complete and valid */
    XGL_PARSE_RESULT_ERROR,      /**< Parse error occurred */
} xgl_parse_result_t;

/**
 * \brief           Frame parser structure
 */
typedef struct {
    xgl_parse_state_t state;       /**< Current parser state */
    uint8_t* cache;                /**< Cache buffer for frame data */
    size_t cache_size;             /**< Total cache buffer size */
    size_t cache_len;              /**< Current data length in cache */
    uint32_t timestamp;            /**< Timestamp when parsing started (ms) */
    uint16_t expected_payload_len; /**< Expected payload length from header */
    uint8_t expected_header_len;   /**< Header length including extensions */
    uint8_t expected_auth_tag_len; /**< Authentication trailer length */
} xgl_parser_t;

/*---------------------------------------------------------------------------*/
/* Parser Configuration                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default parser timeout in milliseconds
 */
#define XGL_PARSER_TIMEOUT_MS 1000

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
xgl_error_t xgl_parser_init(xgl_parser_t* parser, uint8_t* cache_buffer,
                            size_t cache_size);

/**
 * \brief           Reset parser and discard all buffered bytes
 * \param[in,out]   parser: Parser structure to reset
 */
void xgl_parser_reset(xgl_parser_t* parser);

/**
 * \brief           Feed byte to parser
 * \param[in,out]   parser: Parser structure
 * \param[in]       byte: Byte to parse
 * \param[in]       current_time_ms: Current time in milliseconds
 * \return          Parse result (incomplete, complete, or error)
 * \note            Consume COMPLETE before advancing. After COMPLETE or ERROR,
 *                  drain with xgl_parser_advance_view before feeding again.
 */
xgl_parse_result_t xgl_parser_feed_byte(xgl_parser_t* parser, uint8_t byte,
                                        uint32_t current_time_ms);

/**
 * \brief           Feed a byte and expose the complete validated frame
 * \param[in,out]   parser: Per-link parser and caller-owned cache
 * \param[in]       byte: Next input byte
 * \param[in]       current_time_ms: Explicit monotonic time
 * \param[out]      view: Required output, valid only when COMPLETE is returned
 * \param[out]      status: Optional wire diagnostic, OK on COMPLETE, a CRC
 *                  classification on CRC failure, otherwise INVALID
 * \return          Parse result; NULL parser or view, or pending COMPLETE or
 *                  RECOVER, returns ERROR without consuming input.
 * \note            Consume COMPLETE synchronously, then advance until
 * INCOMPLETE after either COMPLETE or ERROR before feeding another byte. The
 * view borrows the cache until advance, reset or next feed. Authentication is
 * performed by the receiving endpoint.
 */
xgl_parse_result_t xgl_parser_feed_byte_view(xgl_parser_t* parser, uint8_t byte,
                                             uint32_t current_time_ms,
                                             xgl_wire_frame_view_t* view,
                                             xgl_wire_decode_status_t* status);

/**
 * \brief           Consume a delivered frame or recover rejected buffered input
 * \param[in,out]   parser: Per-link parser and caller-owned cache
 * \param[out]      view: Required output, valid only on COMPLETE
 * \param[out]      status: Optional diagnostic, OK on COMPLETE, otherwise
 * INVALID
 * \return          COMPLETE for another buffered frame, INCOMPLETE when more
 *                  input is needed, or ERROR for NULL parser or view
 * \note            Call only after consuming the previous COMPLETE or recording
 *                  ERROR. Repeated calls drain cached frames without new input.
 *                  Recovery prefers the earliest complete valid frame,
 * otherwise retains the earliest possible partial candidate. Invalid candidates
 * encountered while scanning are not reported again. Retained bytes inherit the
 * original candidate timestamp; recovery does not extend the absolute deadline.
 * This call invalidates the previous borrowed view and never allocates. Other
 * parser states are unchanged and return INCOMPLETE.
 */
xgl_parse_result_t xgl_parser_advance_view(xgl_parser_t* parser,
                                           xgl_wire_frame_view_t* view,
                                           xgl_wire_decode_status_t* status);

/**
 * \brief           Check for parser timeout
 * \param[in]       parser: Parser structure
 * \param[in]       current_time_ms: Current time in milliseconds
 * \param[in]       timeout_ms: Timeout in milliseconds; zero disables expiry
 * \return          true if timeout occurred, false otherwise
 * \note            The deadline starts at the current candidate's first magic
 *                  byte. Later header or body bytes do not extend it.
 *                  Recovered suffixes conservatively inherit that deadline.
 */
bool xgl_parser_check_timeout(const xgl_parser_t* parser,
                              uint32_t current_time_ms, uint32_t timeout_ms);

/**
 * \brief           Get parsed frame data
 * \param[in]       parser: Parser structure
 * \param[out]      frame_buffer: Output buffer for complete frame
 * \param[out]      frame_len: Length of this frame, excluding buffered suffixes
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_parser_get_frame(const xgl_parser_t* parser,
                                 uint8_t** frame_buffer, size_t* frame_len);

/**
 * \brief           Get parser state (for debugging)
 * \param[in]       parser: Parser structure
 * \return          Current parser state
 */
static inline xgl_parse_state_t
xgl_parser_get_state(const xgl_parser_t* parser) {
    return parser ? parser->state : XGL_PARSE_MAGIC;
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
