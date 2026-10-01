/**
 * \file            xgl_parser_extensions.c
 * \brief           Parser wire-header extension validation helpers
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_wire.h>

#include "xgl_parser_internal.h"

/**
 * \brief           Validate buffered TLVs and determine the expected auth
 *                  trailer
 * \param[in,out]   parser: Parser with the complete header already buffered
 * \return          INCOMPLETE for a valid header, ERROR for invalid extensions
 */
xgl_parse_result_t xgl_parser_validate_header_extensions(xgl_parser_t* parser) {
    parser->expected_auth_tag_len = 0U;

    size_t ext_len = parser->expected_header_len - XGL_WIRE_BASE_HEADER_SIZE;
    if (ext_len == 0U) {
        return XGL_PARSE_RESULT_INCOMPLETE;
    }

    xgl_wire_header_t header;
    if (xgl_wire_decode_header(&header, parser->cache,
                               XGL_WIRE_BASE_HEADER_SIZE) != XGL_OK) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
    }

    xgl_wire_ext_metadata_t metadata;
    xgl_error_t err = xgl_wire_decode_ext_metadata(
        &parser->cache[XGL_WIRE_BASE_HEADER_SIZE], ext_len, &metadata);
    if (err != XGL_OK) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
    }
    parser->expected_auth_tag_len = metadata.auth_tag_len;

    if ((header.flags & XGL_WIRE_FLAG_AUTHENTICATED) != 0U &&
        parser->expected_auth_tag_len == 0U) {
        xgl_parser_reset(parser);
        return XGL_PARSE_RESULT_ERROR;
    }

    return XGL_PARSE_RESULT_INCOMPLETE;
}
