/**
 * \file            xgl_frame_zerocopy.c
 * \brief           Zero-copy frame building implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_wire.h>

#define XGL_FRAME_DEFAULT_TTL 8U

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_frame_build_zerocopy(uint8_t* buffer, size_t buffer_size,
                                     size_t data_offset, size_t data_len,
                                     uint16_t source_id, uint16_t target_id,
                                     uint8_t data_type, uint32_t packet_number,
                                     bool reliable, uint8_t priority,
                                     size_t* frame_len) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || frame_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (data_len > UINT16_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    size_t app_type_ext_len = (data_type != 0U) ? XGL_DATA_TYPE_EXT_SIZE : 0U;
    size_t header_len = XGL_WIRE_BASE_HEADER_SIZE + app_type_ext_len;
    if (header_len > UINT8_MAX || data_offset != header_len) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t required_size = data_offset + data_len + XGL_CRC16_SIZE;
    if (buffer_size < required_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t extensions[XGL_DATA_TYPE_EXT_SIZE];
    if (app_type_ext_len > 0U) {
        size_t written = 0U;
        xgl_error_t error = xgl_wire_encode_ext(extensions, sizeof(extensions),
                                                XGL_WIRE_EXT_DATA_TYPE,
                                                &data_type, 1U, &written);
        if (error != XGL_OK) {
            return error;
        }
    }
    xgl_frame_params_t params = {.source_id = source_id,
                                 .target_id = target_id,
                                 .packet_number = packet_number,
                                 .extensions =
                                     app_type_ext_len > 0U ? extensions : NULL,
                                 .extensions_len = app_type_ext_len,
                                 .payload = buffer + data_offset,
                                 .payload_len = data_len,
                                 .reliable = reliable,
                                 .priority = priority,
                                 .ttl = XGL_FRAME_DEFAULT_TTL};
    xgl_frame_t frame;
    xgl_error_t error = xgl_frame_build(&frame, &params);
    if (error != XGL_OK) {
        return error;
    }
    return xgl_frame_serialize(buffer, buffer_size, &frame, frame_len);
}
