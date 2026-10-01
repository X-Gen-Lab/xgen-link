/**
 * \file            xgl_wire_ext.c
 * \brief           Wire-format extension value encoding primitives
 */

#include <xgl/internal/xgl_wire.h>

#include <string.h>
#include <xgen/bytes/bytes.h>

/**
 * \brief           Decode singleton extension metadata from a bounded TLV span
 * \param[in]       extensions: Extension bytes, or NULL for an empty span
 * \param[in]       extensions_len: Available extension bytes
 * \param[out]      metadata: Decoded values, valid only on success
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_wire_decode_ext_metadata(const uint8_t* extensions,
                                         size_t extensions_len,
                                         xgl_wire_ext_metadata_t* metadata) {
    if (metadata == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(metadata, 0, sizeof(*metadata));
    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err =
        xgl_wire_ext_cursor_init(&cursor, extensions, extensions_len);
    if (err != XGL_OK) {
        return err;
    }
    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        switch (ext.type) {
            case XGL_WIRE_EXT_DATA_TYPE:
                if (metadata->data_type_found || ext.len != 1U) {
                    return XGL_ERR_INVALID_FRAME;
                }
                metadata->data_type = ext.value[0];
                metadata->data_type_found = true;
                break;
            case XGL_WIRE_EXT_SESSION:
                if (metadata->session_epoch_found) {
                    return XGL_ERR_INVALID_FRAME;
                }
                err = xgl_wire_decode_session_ext_value(
                    ext.value, ext.len, &metadata->session_epoch,
                    &metadata->incarnation_id);
                if (err != XGL_OK) {
                    return err;
                }
                metadata->session_epoch_found = true;
                break;
            case XGL_WIRE_EXT_SECURITY:
                if (metadata->has_security_ext) {
                    return XGL_ERR_INVALID_FRAME;
                }
                err = xgl_wire_decode_security_ext_value(
                    ext.value, ext.len, &metadata->auth_key_id,
                    &metadata->nonce_id, &metadata->auth_tag_len);
                if (err != XGL_OK) {
                    return err;
                }
                metadata->has_security_ext = true;
                break;
            default:
                /* Skip unknown noncritical extensions in v3. */
                break;
        }
    }
    return (err == XGL_ERR_NOT_FOUND) ? XGL_OK : err;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_wire_encode_fragment_ext_value(
    uint8_t* buffer, size_t buffer_size, uint32_t message_id,
    uint32_t fragment_offset, uint32_t message_len, size_t* bytes_written) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < XGL_FRAGMENT_EXT_VALUE_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgb_serialize_u32_le(&buffer[0], message_id);
    xgb_serialize_u32_le(&buffer[4], fragment_offset);
    xgb_serialize_u32_le(&buffer[8], message_len);
    *bytes_written = XGL_FRAGMENT_EXT_VALUE_SIZE;

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_fragment_ext_value(const uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t* message_id,
                                               uint32_t* fragment_offset,
                                               uint32_t* message_len) {
    if (buffer == NULL || message_id == NULL || fragment_offset == NULL ||
        message_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != XGL_FRAGMENT_EXT_VALUE_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    *message_id = xgb_deserialize_u32_le(&buffer[0]);
    *fragment_offset = xgb_deserialize_u32_le(&buffer[4]);
    *message_len = xgb_deserialize_u32_le(&buffer[8]);

    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_wire_encode_session_ext_value(uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t session_epoch,
                                              uint64_t incarnation_id,
                                              size_t* bytes_written) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < XGL_SESSION_EXT_VALUE_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgb_serialize_u32_le(&buffer[0], session_epoch);
    xgb_serialize_u64_le(&buffer[4], incarnation_id);
    *bytes_written = XGL_SESSION_EXT_VALUE_SIZE;

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_session_ext_value(const uint8_t* buffer,
                                              size_t buffer_size,
                                              uint32_t* session_epoch,
                                              uint64_t* incarnation_id) {
    if (buffer == NULL || session_epoch == NULL || incarnation_id == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != XGL_SESSION_EXT_VALUE_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    *session_epoch = xgb_deserialize_u32_le(&buffer[0]);
    *incarnation_id = xgb_deserialize_u64_le(&buffer[4]);

    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t
xgl_wire_encode_security_ext_value(uint8_t* buffer, size_t buffer_size,
                                   uint32_t key_id, uint64_t nonce_id,
                                   uint8_t tag_len, size_t* bytes_written) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tag_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (buffer_size < 13U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgb_serialize_u32_le(&buffer[0], key_id);
    xgb_serialize_u64_le(&buffer[4], nonce_id);
    buffer[12] = tag_len;
    *bytes_written = 13U;

    return XGL_OK;
}

xgl_error_t xgl_wire_decode_security_ext_value(const uint8_t* buffer,
                                               size_t buffer_size,
                                               uint32_t* key_id,
                                               uint64_t* nonce_id,
                                               uint8_t* tag_len) {
    if (buffer == NULL || key_id == NULL || nonce_id == NULL ||
        tag_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != 13U) {
        return XGL_ERR_INVALID_FRAME;
    }

    *key_id = xgb_deserialize_u32_le(&buffer[0]);
    *nonce_id = xgb_deserialize_u64_le(&buffer[4]);
    *tag_len = buffer[12];
    if (*tag_len == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_wire_encode_route_ext_value(uint8_t* buffer, size_t buffer_size,
                                            uint16_t previous_hop,
                                            uint16_t next_hop,
                                            uint32_t route_epoch,
                                            uint16_t metric,
                                            size_t* bytes_written) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (buffer == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size < 10U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    xgb_serialize_u16_le(&buffer[0], previous_hop);
    xgb_serialize_u16_le(&buffer[2], next_hop);
    xgb_serialize_u32_le(&buffer[4], route_epoch);
    xgb_serialize_u16_le(&buffer[8], metric);
    *bytes_written = 10U;

    return XGL_OK;
}

xgl_error_t
xgl_wire_decode_route_ext_value(const uint8_t* buffer, size_t buffer_size,
                                uint16_t* previous_hop, uint16_t* next_hop,
                                uint32_t* route_epoch, uint16_t* metric) {
    if (buffer == NULL || previous_hop == NULL || next_hop == NULL ||
        route_epoch == NULL || metric == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (buffer_size != 10U) {
        return XGL_ERR_INVALID_FRAME;
    }

    *previous_hop = xgb_deserialize_u16_le(&buffer[0]);
    *next_hop = xgb_deserialize_u16_le(&buffer[2]);
    *route_epoch = xgb_deserialize_u32_le(&buffer[4]);
    *metric = xgb_deserialize_u16_le(&buffer[8]);

    return XGL_OK;
}
