/**
 * \file            xgl_parser_extensions.c
 * \brief           Parser wire-header extension validation helpers
 * \author          X-Gen Lab
 */

#include "xgl_parser_internal.h"
#include <xgl/internal/xgl_wire.h>

xgl_parse_result_t xgl_parser_validate_header_extensions(xgl_parser_t* parser) {
    parser->expected_auth_tag_len = 0U;

    size_t ext_len = parser->expected_header_len - XGL_WIRE_BASE_HEADER_SIZE;
    if (ext_len == 0U) {
        return XGL_PARSE_RESULT_INCOMPLETE;
    }

    xgl_wire_header_t header;
    if (xgl_wire_decode_header(&header,
                               parser->cache,
                               XGL_WIRE_BASE_HEADER_SIZE) != XGL_OK) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
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
        if (ext.type == XGL_WIRE_EXT_SECURITY) {
            uint32_t key_id = 0U;
            uint64_t nonce_id = 0U;
            uint8_t tag_len = 0U;
            if (xgl_wire_decode_security_ext_value(ext.value,
                                                   ext.len,
                                                   &key_id,
                                                   &nonce_id,
                                                   &tag_len) != XGL_OK ||
                tag_len == 0U) {
                xgl_parser_reset(parser);
                return XGL_PARSE_RESULT_ERROR;
            }
            (void)key_id;
            (void)nonce_id;
            parser->expected_auth_tag_len = tag_len;
        }
    }

    if (err != XGL_ERR_NOT_FOUND) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
    }

    if ((header.flags & XGL_WIRE_FLAG_AUTHENTICATED) != 0U &&
        parser->expected_auth_tag_len == 0U) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
    }

    return XGL_PARSE_RESULT_INCOMPLETE;
}
