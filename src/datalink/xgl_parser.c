/**
 * \file            xgl_parser.c
 * \brief           Per-link bounded byte-stream framing state machine
 * \author          X-Gen Lab
 */

#include <datalink/xgl_parser.h>
#include <wire/xgl_wire.h>
#include <xgl/xgl_config.h>

#include <string.h>

xgl_error_t xgl_parser_init(xgl_parser_t* parser, uint8_t* cache_buffer,
                            size_t cache_size) {
    if (parser == NULL || cache_buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (cache_size < XGL_WIRE_BASE_HEADER_SIZE + XGL_CRC16_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    memset(parser, 0, sizeof(*parser));
    parser->cache = cache_buffer;
    parser->cache_size = cache_size;
    xgl_parser_reset(parser);
    return XGL_OK;
}

void xgl_parser_reset(xgl_parser_t* parser) {
    if (parser == NULL) {
        return;
    }
    parser->state = XGL_PARSE_MAGIC;
    parser->cache_len = 0U;
    parser->index = 0U;
    parser->timestamp = 0U;
    parser->expected_header_len = 0U;
    parser->expected_payload_len = 0U;
    parser->expected_auth_tag_len = 0U;
}

static xgl_parse_result_t parser_reject(xgl_parser_t* parser) {
    xgl_parser_reset(parser);
    return XGL_PARSE_RESULT_ERROR;
}

/** \brief           Determine bounded body length after all TLVs arrive. */
static xgl_parse_result_t parser_finish_header(xgl_parser_t* parser) {
    xgl_wire_ext_metadata_t metadata;
    xgl_error_t error = xgl_wire_decode_ext_metadata(
        parser->cache + XGL_WIRE_BASE_HEADER_SIZE,
        parser->expected_header_len - XGL_WIRE_BASE_HEADER_SIZE, &metadata);
    if (error != XGL_OK) {
        return parser_reject(parser);
    }
    parser->expected_auth_tag_len = metadata.auth_tag_len;
    size_t body_overhead = parser->expected_header_len +
                           parser->expected_auth_tag_len + XGL_CRC16_SIZE;
    if (body_overhead > parser->cache_size ||
        parser->expected_payload_len > parser->cache_size - body_overhead) {
        return parser_reject(parser);
    }
    parser->state =
        parser->expected_payload_len > 0U || parser->expected_auth_tag_len > 0U
            ? XGL_PARSE_PAYLOAD
            : XGL_PARSE_CRC;
    parser->index = 0U;
    return XGL_PARSE_RESULT_INCOMPLETE;
}

/** \brief           Validate without retaining a view in each link object. */
static xgl_error_t
parser_validate_without_view(const xgl_parser_t* parser,
                             xgl_wire_decode_status_t* status) {
    xgl_wire_frame_view_t view;
    return xgl_wire_decode_frame(&view, parser->cache, parser->cache_len,
                                 status);
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_parse_result_t xgl_parser_feed_byte_view(xgl_parser_t* parser, uint8_t byte,
                                             uint32_t current_time_ms,
                                             xgl_wire_frame_view_t* view,
                                             xgl_wire_decode_status_t* status) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_INVALID;
    }
    if (parser == NULL) {
        return XGL_PARSE_RESULT_ERROR;
    }
    if (parser->cache_len >= parser->cache_size) {
        return parser_reject(parser);
    }
    switch (parser->state) {
        case XGL_PARSE_MAGIC:
            if (byte == XGL_WIRE_MAGIC_0) {
                parser->cache[0] = byte;
                parser->cache_len = 1U;
                parser->timestamp = current_time_ms;
                parser->state = XGL_PARSE_HEADER;
            }
            return XGL_PARSE_RESULT_INCOMPLETE;

        case XGL_PARSE_HEADER:
            parser->cache[parser->cache_len++] = byte;
            if (parser->cache_len == 2U && byte != XGL_WIRE_MAGIC_1) {
                if (byte == XGL_WIRE_MAGIC_0) {
                    parser->cache_len = 1U;
                } else {
                    xgl_parser_reset(parser);
                }
                return XGL_PARSE_RESULT_INCOMPLETE;
            }
            if (parser->cache_len == XGL_WIRE_BASE_HEADER_SIZE) {
                xgl_wire_header_t header;
                xgl_error_t error = xgl_wire_decode_header(
                    &header, parser->cache, parser->cache_len);
                if (error != XGL_OK) {
                    if (status != NULL && error == XGL_ERR_CRC_FAILED) {
                        *status = XGL_WIRE_DECODE_HEADER_CRC;
                    }
                    return parser_reject(parser);
                }
                parser->expected_header_len = header.header_len;
                parser->expected_payload_len = header.payload_len;
                if (header.header_len > XGL_WIRE_BASE_HEADER_SIZE) {
                    parser->state = XGL_PARSE_EXTENSIONS;
                } else {
                    return parser_finish_header(parser);
                }
            }
            return XGL_PARSE_RESULT_INCOMPLETE;

        case XGL_PARSE_EXTENSIONS:
            parser->cache[parser->cache_len++] = byte;
            return parser->cache_len == parser->expected_header_len
                       ? parser_finish_header(parser)
                       : XGL_PARSE_RESULT_INCOMPLETE;

        case XGL_PARSE_PAYLOAD:
            parser->cache[parser->cache_len++] = byte;
            if (parser->cache_len - parser->expected_header_len ==
                (size_t)parser->expected_payload_len +
                    parser->expected_auth_tag_len) {
                parser->state = XGL_PARSE_CRC;
                parser->index = 0U;
            }
            return XGL_PARSE_RESULT_INCOMPLETE;

        case XGL_PARSE_CRC:
            parser->cache[parser->cache_len++] = byte;
            if (++parser->index == XGL_CRC16_SIZE) {
                xgl_error_t error =
                    view != NULL
                        ? xgl_wire_decode_frame(view, parser->cache,
                                                parser->cache_len, status)
                        : parser_validate_without_view(parser, status);
                if (error != XGL_OK) {
                    return parser_reject(parser);
                }
                parser->state = XGL_PARSE_COMPLETE;
                return XGL_PARSE_RESULT_COMPLETE;
            }
            return XGL_PARSE_RESULT_INCOMPLETE;

        default:
            return parser_reject(parser);
    }
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_parse_result_t xgl_parser_feed_byte(xgl_parser_t* parser, uint8_t byte,
                                        uint32_t current_time_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    return xgl_parser_feed_byte_view(parser, byte, current_time_ms, NULL, NULL);
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
bool xgl_parser_check_timeout(const xgl_parser_t* parser,
                              uint32_t current_time_ms, uint32_t timeout_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (parser == NULL || parser->state == XGL_PARSE_MAGIC ||
        parser->state == XGL_PARSE_COMPLETE) {
        return false;
    }
    return current_time_ms - parser->timestamp >= timeout_ms;
}

xgl_error_t xgl_parser_get_frame(const xgl_parser_t* parser,
                                 uint8_t** frame_buffer, size_t* frame_len) {
    if (parser == NULL || frame_buffer == NULL || frame_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (parser->state != XGL_PARSE_COMPLETE) {
        return XGL_ERR_INVALID_FRAME;
    }
    *frame_buffer = parser->cache;
    *frame_len = parser->cache_len;
    return XGL_OK;
}
