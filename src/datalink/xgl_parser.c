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
    parser->timestamp = 0U;
    parser->expected_header_len = 0U;
    parser->expected_payload_len = 0U;
    parser->expected_auth_tag_len = 0U;
}

/** \brief           Find a magic pair or a final byte that may start one. */
static size_t parser_find_magic(const xgl_parser_t* parser, size_t offset) {
    for (; offset < parser->cache_len; ++offset) {
        if (parser->cache[offset] == XGL_WIRE_MAGIC_0 &&
            (offset + 1U == parser->cache_len ||
             parser->cache[offset + 1U] == XGL_WIRE_MAGIC_1)) {
            break;
        }
    }
    return offset;
}

static xgl_parse_result_t parser_reject(xgl_parser_t* parser) {
    if (parser_find_magic(parser, 1U) < parser->cache_len) {
        parser->state = XGL_PARSE_RECOVER;
    } else {
        xgl_parser_reset(parser);
    }
    return XGL_PARSE_RESULT_ERROR;
}

static size_t parser_frame_len(const xgl_parser_t* parser) {
    return (size_t)parser->expected_header_len + parser->expected_payload_len +
           parser->expected_auth_tag_len + XGL_CRC16_SIZE;
}

/** \brief           Validate received boundaries without moving buffered bytes.
 */
static xgl_parse_result_t
parser_inspect(xgl_parser_t* parser, const uint8_t* buffer, size_t available,
               xgl_wire_frame_view_t* view, xgl_wire_decode_status_t* status) {
    if (parser->state == XGL_PARSE_HEADER) {
        if (available < XGL_WIRE_BASE_HEADER_SIZE) {
            return XGL_PARSE_RESULT_INCOMPLETE;
        }
        xgl_error_t error =
            xgl_wire_decode_header(&view->header, buffer, available);
        if (error != XGL_OK) {
            if (status != NULL && error == XGL_ERR_CRC_FAILED) {
                *status = XGL_WIRE_DECODE_HEADER_CRC;
            }
            return XGL_PARSE_RESULT_ERROR;
        }
        const size_t minimum_frame_len =
            (size_t)view->header.header_len + XGL_CRC16_SIZE;
        if (minimum_frame_len > parser->cache_size ||
            view->header.payload_len > parser->cache_size - minimum_frame_len) {
            return XGL_PARSE_RESULT_ERROR;
        }
        parser->expected_header_len = view->header.header_len;
        parser->expected_payload_len = view->header.payload_len;
        parser->expected_auth_tag_len = 0U;
        parser->state = XGL_PARSE_EXTENSIONS;
    }
    if (parser->state == XGL_PARSE_EXTENSIONS) {
        if (available < parser->expected_header_len) {
            return XGL_PARSE_RESULT_INCOMPLETE;
        }
        xgl_wire_ext_metadata_t metadata;
        if (xgl_wire_decode_ext_metadata(buffer + XGL_WIRE_BASE_HEADER_SIZE,
                                         (size_t)parser->expected_header_len -
                                             XGL_WIRE_BASE_HEADER_SIZE,
                                         &metadata) != XGL_OK) {
            return XGL_PARSE_RESULT_ERROR;
        }
        parser->expected_auth_tag_len = metadata.auth_tag_len;
        const size_t body_overhead = (size_t)parser->expected_header_len +
                                     parser->expected_auth_tag_len +
                                     XGL_CRC16_SIZE;
        if (body_overhead > parser->cache_size ||
            parser->expected_payload_len > parser->cache_size - body_overhead) {
            return XGL_PARSE_RESULT_ERROR;
        }
        parser->state = XGL_PARSE_PAYLOAD;
    }
    if (parser->state == XGL_PARSE_PAYLOAD) {
        if (available < parser_frame_len(parser) - XGL_CRC16_SIZE) {
            return XGL_PARSE_RESULT_INCOMPLETE;
        }
        parser->state = XGL_PARSE_CRC;
    }
    if (parser->state != XGL_PARSE_CRC) {
        return XGL_PARSE_RESULT_ERROR;
    }
    if (available < parser_frame_len(parser)) {
        return XGL_PARSE_RESULT_INCOMPLETE;
    }
    if (xgl_wire_decode_frame(view, buffer, parser_frame_len(parser), status) !=
        XGL_OK) {
        return XGL_PARSE_RESULT_ERROR;
    }
    parser->state = XGL_PARSE_COMPLETE;
    return XGL_PARSE_RESULT_COMPLETE;
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
    if (parser == NULL || view == NULL || parser->state == XGL_PARSE_COMPLETE ||
        parser->state == XGL_PARSE_RECOVER) {
        return XGL_PARSE_RESULT_ERROR;
    }
    if (parser->cache_len >= parser->cache_size) {
        return parser_reject(parser);
    }
    if (parser->state == XGL_PARSE_MAGIC) {
        if (byte == XGL_WIRE_MAGIC_0) {
            parser->cache[0] = byte;
            parser->cache_len = 1U;
            parser->timestamp = current_time_ms;
            parser->state = XGL_PARSE_HEADER;
        }
        return XGL_PARSE_RESULT_INCOMPLETE;
    }
    parser->cache[parser->cache_len++] = byte;
    if (parser->state == XGL_PARSE_HEADER && parser->cache_len == 2U &&
        byte != XGL_WIRE_MAGIC_1) {
        if (byte == XGL_WIRE_MAGIC_0) {
            parser->cache_len = 1U;
            parser->timestamp = current_time_ms;
        } else {
            xgl_parser_reset(parser);
        }
        return XGL_PARSE_RESULT_INCOMPLETE;
    }
    xgl_parse_result_t result =
        parser_inspect(parser, parser->cache, parser->cache_len, view, status);
    return result == XGL_PARSE_RESULT_ERROR ? parser_reject(parser) : result;
}

/** \brief           Compact once after choosing a frame or partial candidate.
 */
static void parser_retain_suffix(xgl_parser_t* parser, size_t offset) {
    parser->cache_len -= offset;
    if (offset != 0U) {
        memmove(parser->cache, parser->cache + offset, parser->cache_len);
    }
}

xgl_parse_result_t xgl_parser_advance_view(xgl_parser_t* parser,
                                           xgl_wire_frame_view_t* view,
                                           xgl_wire_decode_status_t* status) {
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_INVALID;
    }
    if (parser == NULL || view == NULL) {
        return XGL_PARSE_RESULT_ERROR;
    }
    size_t offset;
    if (parser->state == XGL_PARSE_COMPLETE) {
        offset = parser_frame_len(parser);
    } else if (parser->state == XGL_PARSE_RECOVER) {
        offset = 1U;
    } else {
        return XGL_PARSE_RESULT_INCOMPLETE;
    }

    size_t partial = parser->cache_len;
    for (offset = parser_find_magic(parser, offset); offset < parser->cache_len;
         offset = parser_find_magic(parser, offset + 1U)) {
        parser->state = XGL_PARSE_HEADER;
        xgl_parse_result_t result =
            parser_inspect(parser, parser->cache + offset,
                           parser->cache_len - offset, view, NULL);
        if (result == XGL_PARSE_RESULT_COMPLETE) {
            parser_retain_suffix(parser, offset);
            view->frame_buf = parser->cache;
            view->payload = parser->cache + parser->expected_header_len;
            view->extensions = view->extensions_len > 0U
                                   ? parser->cache + XGL_WIRE_BASE_HEADER_SIZE
                                   : NULL;
            if (status != NULL) {
                *status = XGL_WIRE_DECODE_OK;
            }
            return XGL_PARSE_RESULT_COMPLETE;
        }
        if (result == XGL_PARSE_RESULT_INCOMPLETE &&
            partial == parser->cache_len) {
            partial = offset;
        }
    }
    if (partial < parser->cache_len) {
        parser_retain_suffix(parser, partial);
        parser->state = XGL_PARSE_HEADER;
        /* Restore the earliest partial candidate after checking later ones. */
        return parser_inspect(parser, parser->cache, parser->cache_len, view,
                              NULL);
    }
    xgl_parser_reset(parser);
    return XGL_PARSE_RESULT_INCOMPLETE;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_parse_result_t xgl_parser_feed_byte(xgl_parser_t* parser, uint8_t byte,
                                        uint32_t current_time_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    xgl_wire_frame_view_t view;
    return xgl_parser_feed_byte_view(parser, byte, current_time_ms, &view,
                                     NULL);
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
bool xgl_parser_check_timeout(const xgl_parser_t* parser,
                              uint32_t current_time_ms, uint32_t timeout_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (parser == NULL || timeout_ms == 0U ||
        parser->state == XGL_PARSE_MAGIC ||
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
    *frame_len = parser_frame_len(parser);
    return XGL_OK;
}
