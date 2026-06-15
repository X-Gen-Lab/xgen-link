/**
 * \file            xgl_send.c
 * \brief           Send API implementation
 * \author          Nexus Team
 */

#include <xgl/xgl.h>
#include <xgl/internal/xgl_frame.h>
#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_network.h>
#include <xgl/internal/xgl_crc.h>
#include "xgl_instance_internal.h"
#include <string.h>

#define XGL_SEND_DEFAULT_TTL 8U
#define XGL_SEND_SECURITY_EXT_LEN (XGL_WIRE_EXT_HEADER_SIZE + 13U)

/*---------------------------------------------------------------------------*/
/* Parameter Validation Helpers                                              */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Validate standard transmission data
 * \details         Checks all parameters for validity
 */
static xgl_error_t validate_tx_data(const xgl_tx_data_t* tx_data) {
    /* Check for NULL pointer */
    if (tx_data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check data pointer and length */
    if (tx_data->data == NULL && tx_data->data_len > 0) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check data length is not zero */
    if (tx_data->data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Check priority is in valid range (0-7) */
    if (tx_data->priority > 7) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    return XGL_OK;
}

/**
 * \brief           Validate zero-copy transmission data
 * \details         Checks all parameters for validity
 */
static xgl_error_t validate_tx_data_zerocopy(const xgl_tx_data_zerocopy_t* tx_data) {
    /* Check for NULL pointer */
    if (tx_data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check buffer pointer */
    if (tx_data->buffer == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check data length is not zero */
    if (tx_data->data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Check data offset reserves at least the fixed production header. */
    if (tx_data->data_offset < XGL_FRAME_HEADER_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Check buffer size is sufficient */
    if (tx_data->buffer_size < tx_data->data_offset + tx_data->data_len + XGL_CRC16_SIZE) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Check priority is in valid range (0-7) */
    if (tx_data->priority > 7) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Standard Send API (with copy)                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Send data (standard mode with copy)
 * \details         Copies data internally and sends through transport layer
 */
xgl_error_t xgl_send(xgl_handle_t handle, const xgl_tx_data_t* tx_data) {
    xgl_error_t err;
    
    /* Validate handle */
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check if initialized */
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
    /* Validate transmission data */
    err = validate_tx_data(tx_data);
    if (err != XGL_OK) {
        return err;
    }
    
#ifdef XGL_THREAD_SAFE
    /* Lock mutex if thread safety is enabled */
    if (handle->config.features.thread_safe) {
        err = xgl_mutex_lock(&handle->mutex);
        if (err != XGL_OK) {
            return err;
        }
    }
#endif
    
    /* Send through transport layer */
    err = xgl_transport_send(&handle->layers.transport_ctx, handle, tx_data);
    
#ifdef XGL_THREAD_SAFE
    /* Unlock mutex if thread safety is enabled */
    if (handle->config.features.thread_safe) {
        xgl_mutex_unlock(&handle->mutex);
    }
#endif
    
    return err;
}

/*---------------------------------------------------------------------------*/
/* Zero-Copy Send API                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Send data (zero-copy mode)
 * \details         Uses pre-allocated buffer with header space, no data copy
 */
xgl_error_t xgl_send_zerocopy(xgl_handle_t handle, 
                              const xgl_tx_data_zerocopy_t* tx_data) {
    xgl_error_t err;
    
    /* Validate handle */
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check if initialized */
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
    /* Validate zero-copy transmission data */
    err = validate_tx_data_zerocopy(tx_data);
    if (err != XGL_OK) {
        return err;
    }
    
#ifdef XGL_THREAD_SAFE
    /* Lock mutex if thread safety is enabled */
    if (handle->config.features.thread_safe) {
        err = xgl_mutex_lock(&handle->mutex);
        if (err != XGL_OK) {
            return err;
        }
    }
#endif
    
    if (tx_data->reliable) {
        err = XGL_ERR_INVALID_PARAM;
    } else {
        xgl_route_item_t* route = xgl_route_table_lookup(&handle->route_table,
                                                         tx_data->target_id);
        if (route == NULL) {
            err = XGL_ERR_ROUTE_NOT_FOUND;
        } else if (route->phy == NULL || route->phy->tx == NULL) {
            err = XGL_ERR_INVALID_PARAM;
        } else if (handle->config.auth_required &&
                   (handle->config.auth_provider == NULL ||
                    handle->config.auth_provider->sign == NULL ||
                    handle->config.auth_provider->tag_len == 0U)) {
            err = XGL_ERR_INVALID_PARAM;
        } else if (!handle->config.auth_required &&
                   xgl_frame_calculate_size(tx_data->data_len) > route->max_frame_size) {
            err = XGL_ERR_BUFFER_TOO_SMALL;
        } else if (handle->config.auth_required &&
                   XGL_WIRE_BASE_HEADER_SIZE + XGL_SEND_SECURITY_EXT_LEN +
                   tx_data->data_len + handle->config.auth_provider->tag_len +
                   XGL_CRC16_SIZE > route->max_frame_size) {
            err = XGL_ERR_BUFFER_TOO_SMALL;
        } else {
            size_t frame_len = 0;
            if (handle->config.auth_required) {
                size_t auth_header_len = XGL_WIRE_BASE_HEADER_SIZE +
                                         XGL_SEND_SECURITY_EXT_LEN;
                if (tx_data->data_offset != auth_header_len) {
                    err = XGL_ERR_INVALID_PARAM;
                } else {
                    xgl_frame_t frame;
                    xgl_frame_params_t params = {
                        .source_id = handle->config.source_id,
                        .target_id = tx_data->target_id,
                        .data_type = tx_data->data_type,
                        .packet_type = XGL_PACKET_TYPE_DATA,
                        .payload = &tx_data->buffer[tx_data->data_offset],
                        .payload_len = tx_data->data_len,
                        .reliable = false,
                        .priority = tx_data->priority,
                        .ttl = XGL_SEND_DEFAULT_TTL
                    };

                    err = xgl_frame_build(&frame, &params);
                    if (err == XGL_OK) {
                        err = xgl_frame_serialize_authenticated(tx_data->buffer,
                                                                tx_data->buffer_size,
                                                                &frame,
                                                                handle->config.auth_key_id,
                                                                handle->config.auth_provider,
                                                                &frame_len);
                    }
                }
            } else {
                err = xgl_frame_build_zerocopy(tx_data->buffer,
                                               tx_data->buffer_size,
                                               tx_data->data_offset,
                                               tx_data->data_len,
                                               handle->config.source_id,
                                               tx_data->target_id,
                                               tx_data->data_type,
                                               0,
                                               false,
                                               tx_data->priority,
                                               &frame_len);
            }
            if (err == XGL_OK) {
                err = xgl_datalink_send_raw(&handle->layers.datalink_ctx,
                                            route->phy,
                                            tx_data->buffer,
                                            frame_len);
            }
            if (err == XGL_OK) {
                handle->stats.transport.tx_packets++;
                handle->stats.transport.tx_bytes += tx_data->data_len;
                handle->stats.network.tx_packets++;
                handle->stats.network.tx_bytes += tx_data->data_len;
            }
        }
    }
    
#ifdef XGL_THREAD_SAFE
    /* Unlock mutex if thread safety is enabled */
    if (handle->config.features.thread_safe) {
        xgl_mutex_unlock(&handle->mutex);
    }
#endif
    
    return err;
}
