/**
 * \file            xgl_send_zerocopy.c
 * \brief           Zero-copy send API implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/xgl.h>

#include "xgl_instance_internal.h"

#define XGL_SEND_DEFAULT_TTL 8U

/**
 * \brief           Validate zero-copy transmission data
 * \param[in]       tx_data: Borrowed caller buffer and send parameters
 * \return          XGL_OK when lengths and offsets are valid
 */
static xgl_error_t
validate_tx_data_zerocopy(const xgl_tx_data_zerocopy_t* tx_data) {
    if (tx_data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tx_data->buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (tx_data->data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (tx_data->data_offset < XGL_FRAME_HEADER_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (tx_data->data_offset > tx_data->buffer_size ||
        tx_data->data_len > tx_data->buffer_size - tx_data->data_offset ||
        XGL_CRC16_SIZE >
            tx_data->buffer_size - tx_data->data_offset - tx_data->data_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    if (tx_data->priority > 7) {
        return XGL_ERR_INVALID_PARAM;
    }

    return XGL_OK;
}

/**
 * \brief           Send data (zero-copy mode)
 * \param[in]       handle: Initialized instance
 * \param[in,out]   tx_data: Caller-owned buffer valid until synchronous TX
 * returns
 * \param[in]       now_ms: Caller-supplied monotonic 32-bit time
 * \return          XGL_OK after synchronous PHY acceptance, error code
 * otherwise
 */
xgl_error_t xgl_send_zerocopy_at(xgl_handle_t handle,
                                 const xgl_tx_data_zerocopy_t* tx_data,
                                 uint32_t now_ms) {
    xgl_error_t err;

    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }

    err = validate_tx_data_zerocopy(tx_data);
    if (err != XGL_OK) {
        return err;
    }

    handle->layers.transport_ctx.current_time_ms = now_ms;

    if (tx_data->reliable) {
        err = XGL_ERR_INVALID_PARAM;
    } else {
        xgl_route_item_t* route =
            xgl_route_table_lookup(&handle->route_table, tx_data->target_id);
        if (route == NULL) {
            err = XGL_ERR_ROUTE_NOT_FOUND;
        } else if (route->phy == NULL || route->phy->tx == NULL ||
                   (handle->config->auth_required &&
                    (handle->config->auth_provider == NULL ||
                     handle->config->auth_provider->sign == NULL ||
                     handle->config->auth_provider->tag_len == 0U ||
                     handle->config->auth_provider->tag_len >
                         XGL_AUTH_TAG_MAX_LEN))) {
            err = XGL_ERR_INVALID_PARAM;
        } else {
            size_t app_type_ext_len =
                (tx_data->data_type != 0U) ? XGL_DATA_TYPE_EXT_SIZE : 0U;
#if XGL_FEATURE_AUTH
            size_t unauth_header_len =
                XGL_WIRE_BASE_HEADER_SIZE + app_type_ext_len;
#endif
            size_t auth_tag_len = handle->config->auth_required
                                      ? handle->config->auth_provider->tag_len
                                      : 0U;
            size_t serialized_len = xgl_frame_serialized_size(
                tx_data->data_len, app_type_ext_len, auth_tag_len);
#if XGL_FEATURE_AUTH
            if (handle->config->auth_required) {
                unauth_header_len += XGL_SECURITY_EXT_SIZE;
            }
#endif
            if (serialized_len > route->max_frame_size ||
                serialized_len > handle->config->protocol.max_frame_size) {
                err = XGL_ERR_BUFFER_TOO_SMALL;
                goto zerocopy_done;
            }

            size_t frame_len = 0;
#if XGL_FEATURE_AUTH
            if (handle->config->auth_required) {
                size_t auth_header_len = unauth_header_len;
                if (tx_data->data_offset != auth_header_len) {
                    err = XGL_ERR_INVALID_PARAM;
                } else {
                    xgl_frame_t frame;
                    uint8_t app_type_ext[XGL_DATA_TYPE_EXT_SIZE] = {0};
                    size_t app_type_ext_written = 0U;
                    const uint8_t* frame_extensions = NULL;
                    size_t frame_extensions_len = 0U;
                    if (tx_data->data_type != 0U) {
                        err = xgl_wire_encode_ext(
                            app_type_ext, sizeof(app_type_ext),
                            XGL_WIRE_EXT_DATA_TYPE, &tx_data->data_type, 1U,
                            &app_type_ext_written);
                        if (err != XGL_OK) {
                            goto zerocopy_done;
                        }
                        frame_extensions = app_type_ext;
                        frame_extensions_len = app_type_ext_written;
                    }

                    xgl_frame_params_t params = {
                        .source_id = handle->config->source_id,
                        .target_id = tx_data->target_id,
                        .data_type = tx_data->data_type,
                        .packet_type = XGL_PACKET_TYPE_DATA,
                        .extensions = frame_extensions,
                        .extensions_len = frame_extensions_len,
                        .payload = &tx_data->buffer[tx_data->data_offset],
                        .payload_len = tx_data->data_len,
                        .reliable = false,
                        .priority = tx_data->priority,
                        .ttl = XGL_SEND_DEFAULT_TTL};

                    err = xgl_frame_build(&frame, &params);
                    if (err == XGL_OK) {
                        err = xgl_frame_serialize_authenticated(
                            tx_data->buffer, tx_data->buffer_size, &frame,
                            &handle->layers.datalink_ctx.security, &frame_len);
                    }
                }
            } else
#endif
            {
                err = xgl_frame_build_zerocopy(
                    tx_data->buffer, tx_data->buffer_size, tx_data->data_offset,
                    tx_data->data_len, handle->config->source_id,
                    tx_data->target_id, tx_data->data_type, 0, false,
                    tx_data->priority, &frame_len);
            }
            if (err == XGL_OK) {
                err = xgl_datalink_send_raw(&handle->layers.datalink_ctx,
                                            route->phy, tx_data->buffer,
                                            frame_len);
            }
            if (err == XGL_OK) {
                handle->stats.transport.tx_packets++;
                handle->stats.transport.tx_bytes += tx_data->data_len;
                handle->stats.network.tx_packets++;
                handle->stats.network.tx_bytes += tx_data->data_len;
            }
        zerocopy_done:;
        }
    }

    return err;
}
