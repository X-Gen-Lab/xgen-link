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
#if XGL_FEATURE_ADVANCED_RUNTIME
    parser->input_offset = 0U;
    parser->input_end = 0U;
    parser->recovery_offset = 0U;
    parser->recovery_partial = 0U;
    parser->input_timestamp = 0U;
    parser->recovery_active = false;
#endif
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
    parser->state = XGL_PARSE_RECOVER;
    return XGL_PARSE_RESULT_ERROR;
}

static size_t parser_frame_len(const xgl_parser_t* parser) {
    return (size_t)parser->expected_header_len + parser->expected_payload_len +
           parser->expected_auth_tag_len + XGL_CRC16_SIZE;
}

/** \brief           Inspect boundaries without moving buffered bytes. */
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
            return parser_reject(parser);
        }
        const size_t minimum_frame_len =
            (size_t)view->header.header_len + XGL_CRC16_SIZE;
        if (minimum_frame_len > parser->cache_size ||
            view->header.payload_len > parser->cache_size - minimum_frame_len) {
            return parser_reject(parser);
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
            return parser_reject(parser);
        }
        /* The fixed-header check already proved this base fits the cache. */
        const size_t base_size = (size_t)parser->expected_header_len +
                                 parser->expected_payload_len + XGL_CRC16_SIZE;
        if (metadata.auth_tag_len > parser->cache_size - base_size) {
            return parser_reject(parser);
        }
        parser->expected_auth_tag_len = metadata.auth_tag_len;
        parser->state = XGL_PARSE_PAYLOAD;
    }
    if (parser->state != XGL_PARSE_PAYLOAD && parser->state != XGL_PARSE_CRC) {
        return parser_reject(parser);
    }
    const size_t total = parser_frame_len(parser);
    if (available < total) {
        parser->state = available < total - XGL_CRC16_SIZE ? XGL_PARSE_PAYLOAD
                                                           : XGL_PARSE_CRC;
        return XGL_PARSE_RESULT_INCOMPLETE;
    }
    if (xgl_wire_decode_frame(view, buffer, total, status) != XGL_OK) {
        return parser_reject(parser);
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
    return parser_inspect(parser, parser->cache, parser->cache_len, view,
                          status);
}

/** \brief           Retain a selected suffix with an offset of at least one. */
static void parser_retain_suffix(xgl_parser_t* parser, size_t offset) {
    parser->cache_len -= offset;
    memmove(parser->cache, parser->cache + offset, parser->cache_len);
}

#if XGL_FEATURE_ADVANCED_RUNTIME
/** \brief           Reset framing without discarding an unread input tail. */
static void parser_reset_candidate(xgl_parser_t* parser) {
    const size_t input_offset = parser->input_offset;
    const size_t input_end = parser->input_end;
    const uint32_t input_timestamp = parser->input_timestamp;
    xgl_parser_reset(parser);
    parser->input_offset = input_offset;
    parser->input_end = input_end;
    parser->input_timestamp = input_timestamp;
}

/** \brief           Continue recovery without moving unexamined candidates. */
static xgl_parse_result_t
parser_advance_bounded(xgl_parser_t* parser, size_t* candidates,
                       xgl_wire_frame_view_t* view,
                       xgl_wire_decode_status_t* status) {
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_INVALID;
    }
    if (parser == NULL || candidates == NULL || view == NULL) {
        return XGL_PARSE_RESULT_ERROR;
    }
    if (!parser->recovery_active) {
        if (parser->state == XGL_PARSE_COMPLETE) {
            parser->recovery_offset = parser_frame_len(parser);
        } else if (parser->state == XGL_PARSE_RECOVER) {
            parser->recovery_offset = 1U;
        } else {
            return XGL_PARSE_RESULT_INCOMPLETE;
        }
        parser->recovery_partial = parser->cache_len;
        parser->recovery_active = true;
    }
    for (;;) {
        const size_t offset =
            parser_find_magic(parser, parser->recovery_offset);
        parser->recovery_offset = offset;
        if (offset == parser->cache_len) {
            break;
        }
        if (*candidates == 0U) {
            parser->state = XGL_PARSE_RECOVER;
            return XGL_PARSE_RESULT_PENDING;
        }
        --*candidates;
        parser->state = XGL_PARSE_HEADER;
        xgl_parse_result_t result =
            parser_inspect(parser, parser->cache + offset,
                           parser->cache_len - offset, view, status);
        if (result == XGL_PARSE_RESULT_COMPLETE) {
            parser->recovery_active = false;
            parser_retain_suffix(parser, offset);
            view->frame_buf = parser->cache;
            view->payload = parser->cache + parser->expected_header_len;
            view->extensions = view->extensions_len > 0U
                                   ? parser->cache + XGL_WIRE_BASE_HEADER_SIZE
                                   : NULL;
            return result;
        }
        if (result == XGL_PARSE_RESULT_INCOMPLETE &&
            parser->recovery_partial == parser->cache_len) {
            parser->recovery_partial = offset;
        }
        parser->recovery_offset = offset + 1U;
        if (status != NULL) {
            *status = XGL_WIRE_DECODE_INVALID;
        }
    }
    if (parser->recovery_partial < parser->cache_len) {
        if (*candidates == 0U) {
            parser->state = XGL_PARSE_RECOVER;
            return XGL_PARSE_RESULT_PENDING;
        }
        --*candidates;
        parser_retain_suffix(parser, parser->recovery_partial);
        parser->recovery_active = false;
        parser->state = XGL_PARSE_HEADER;
        return parser_inspect(parser, parser->cache, parser->cache_len, view,
                              status);
    }
    parser_reset_candidate(parser);
    return XGL_PARSE_RESULT_INCOMPLETE;
}

bool xgl_parser_has_pending(const xgl_parser_t* parser) {
    return parser != NULL &&
           (parser->input_offset < parser->input_end ||
            parser->recovery_active || parser->state == XGL_PARSE_RECOVER ||
            (parser->state == XGL_PARSE_COMPLETE &&
             parser->cache_len > parser_frame_len(parser)));
}

/** \brief           Determine whether the next buffered byte triggers a check.
 */
static bool parser_next_byte_checks(const xgl_parser_t* parser) {
    const size_t available = parser->cache_len + 1U;
    if (parser->state == XGL_PARSE_HEADER) {
        return available >= XGL_WIRE_BASE_HEADER_SIZE;
    }
    if (parser->state == XGL_PARSE_EXTENSIONS) {
        return available >= parser->expected_header_len;
    }
    if (parser->state == XGL_PARSE_PAYLOAD || parser->state == XGL_PARSE_CRC) {
        return available >= parser_frame_len(parser);
    }
    return false;
}

xgl_parse_result_t xgl_parser_buffered_view(xgl_parser_t* parser,
                                            size_t* candidates,
                                            xgl_wire_frame_view_t* view,
                                            xgl_wire_decode_status_t* status) {
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_INVALID;
    }
    if (parser == NULL || candidates == NULL || view == NULL) {
        return XGL_PARSE_RESULT_ERROR;
    }
    xgl_parse_result_t result =
        parser_advance_bounded(parser, candidates, view, status);
    if (result != XGL_PARSE_RESULT_INCOMPLETE) {
        return result;
    }
    while (parser->input_offset < parser->input_end) {
        if (parser_next_byte_checks(parser)) {
            if (*candidates == 0U) {
                return XGL_PARSE_RESULT_PENDING;
            }
            --*candidates;
        }
        const size_t next = parser->input_offset + 1U;
        const size_t end = parser->input_end;
        const uint32_t timestamp = parser->input_timestamp;
        const uint8_t byte = parser->cache[parser->input_offset];
        result =
            xgl_parser_feed_byte_view(parser, byte, timestamp, view, status);
        /* Legacy magic rejection resets framing; retain the input batch. */
        parser->input_offset = next;
        parser->input_end = end;
        parser->input_timestamp = timestamp;
        if (result != XGL_PARSE_RESULT_INCOMPLETE) {
            return result;
        }
    }
    return XGL_PARSE_RESULT_INCOMPLETE;
}

size_t xgl_parser_input_space(xgl_parser_t* parser, uint8_t** buffer) {
    const size_t unread = parser->input_end - parser->input_offset;
    if (unread != 0U) {
        memmove(parser->cache + parser->cache_len,
                parser->cache + parser->input_offset, unread);
    }
    parser->input_offset = parser->cache_len;
    parser->input_end = parser->cache_len + unread;
    *buffer = parser->cache + parser->input_end;
    return parser->cache_size - parser->input_end;
}

/* Count and arrival time follow the bounded input contract. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
void xgl_parser_commit_input(xgl_parser_t* parser, size_t length,
                             uint32_t now_ms) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    parser->input_end += length;
    parser->input_timestamp = now_ms;
}
#endif

xgl_parse_result_t xgl_parser_advance_view(xgl_parser_t* parser,
                                           xgl_wire_frame_view_t* view,
                                           xgl_wire_decode_status_t* status) {
#if XGL_FEATURE_ADVANCED_RUNTIME
    size_t candidates = SIZE_MAX;
    return parser_advance_bounded(parser, &candidates, view, status);
#else
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
                           parser->cache_len - offset, view, status);
        if (result == XGL_PARSE_RESULT_COMPLETE) {
            parser_retain_suffix(parser, offset);
            view->frame_buf = parser->cache;
            view->payload = parser->cache + parser->expected_header_len;
            view->extensions = view->extensions_len > 0U
                                   ? parser->cache + XGL_WIRE_BASE_HEADER_SIZE
                                   : NULL;
            return XGL_PARSE_RESULT_COMPLETE;
        }
        if (result == XGL_PARSE_RESULT_INCOMPLETE &&
            partial == parser->cache_len) {
            partial = offset;
        }
    }
    if (status != NULL) {
        *status = XGL_WIRE_DECODE_INVALID;
    }
    if (partial < parser->cache_len) {
        parser_retain_suffix(parser, partial);
        parser->state = XGL_PARSE_HEADER;
        /* Restore the earliest partial candidate after checking later ones. */
        return parser_inspect(parser, parser->cache, parser->cache_len, view,
                              status);
    }
    xgl_parser_reset(parser);
    return XGL_PARSE_RESULT_INCOMPLETE;
#endif
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
