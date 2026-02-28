/**
 * \file            xgl_send.c
 * \brief           Send API implementation
 * \author          Nexus Team
 */

#include <xgl/xgl.h>
#include <xgl/xgl_frame.h>
#include <xgl/xgl_datalink.h>
#include <xgl/xgl_route.h>
#include "xgl_instance_internal.h"
#include <string.h>

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
    
    /* Check data offset is correct (must be XGL_FRAME_HEADER_SIZE) */
    if (tx_data->data_offset != XGL_FRAME_HEADER_SIZE) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Check buffer size is sufficient */
    if (tx_data->buffer_size < tx_data->data_offset + tx_data->data_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Check priority is in valid range (0-7) */
    if (tx_data->priority > 7) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Create and send frame
 * \details         Builds frame and sends through data link layer
 */
static xgl_error_t create_and_send_frame(xgl_handle_t handle,
                                         const xgl_tx_data_t* tx_data) {
    xgl_frame_t frame;
    xgl_error_t err;
    xgl_route_item_t* route;
    uint8_t seq_num;
    
    /* Look up route for target */
    route = xgl_route_table_lookup(&handle->route_table, tx_data->target_id);
    if (route == NULL) {
        /* Invoke error callback if registered */
        if (handle->config.error_callback != NULL) {
            handle->config.error_callback(handle, XGL_ERR_ROUTE_NOT_FOUND,
                                         "Route not found for target",
                                         handle->config.callback_user_data);
        }
        return XGL_ERR_ROUTE_NOT_FOUND;
    }
    
    /* Get and increment sequence number for this target */
    seq_num = handle->seq_numbers[tx_data->target_id];
    handle->seq_numbers[tx_data->target_id]++;
    
    /* Build frame from transmission data */
    err = xgl_frame_build(&frame,
                         handle->config.source_id,
                         tx_data->target_id,
                         tx_data->data_type,
                         seq_num,
                         0,  /* ACK number is 0 for data packets */
                         tx_data->data,
                         tx_data->data_len,
                         tx_data->reliable,
                         tx_data->priority);
    
    if (err != XGL_OK) {
        return err;
    }
    
    /* Send frame through data link layer */
    err = xgl_datalink_send(route->phy,
                           &frame,
                           &handle->stats,
                           handle->config.error_callback,
                           handle->config.callback_user_data);
    
    /* If reliable transmission and send succeeded, add to wait-ACK list */
    /* TODO: This will be implemented when transport layer integration is complete */
    /* For now, we just send the frame */
    
    return err;
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
    if (handle->config.thread_safe) {
        err = xgl_mutex_lock(&handle->mutex);
        if (err != XGL_OK) {
            return err;
        }
    }
#endif
    
    /* Check if data length exceeds maximum frame size */
    if (tx_data->data_len > handle->config.max_frame_size) {
        /* Fragmentation required */
        if (!handle->config.enable_fragmentation) {
            err = XGL_ERR_BUFFER_TOO_SMALL;
            goto unlock_and_return;
        }
        /* TODO: Implement fragmentation in future task */
        err = XGL_ERR_BUFFER_TOO_SMALL;
        goto unlock_and_return;
    }
    
    /* Create and send frame */
    err = create_and_send_frame(handle, tx_data);
    
    /* Update statistics */
    if (err == XGL_OK) {
        handle->stats.tx_packets++;
        handle->stats.tx_bytes += tx_data->data_len;
    } else {
        handle->stats.tx_errors++;
    }
    
unlock_and_return:
#ifdef XGL_THREAD_SAFE
    /* Unlock mutex if thread safety is enabled */
    if (handle->config.thread_safe) {
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
    xgl_tx_data_t standard_tx_data;
    
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
    if (handle->config.thread_safe) {
        err = xgl_mutex_lock(&handle->mutex);
        if (err != XGL_OK) {
            return err;
        }
    }
#endif
    
    /* Check if data length exceeds maximum frame size */
    if (tx_data->data_len > handle->config.max_frame_size) {
        /* Fragmentation required */
        if (!handle->config.enable_fragmentation) {
            err = XGL_ERR_BUFFER_TOO_SMALL;
            goto unlock_and_return;
        }
        /* TODO: Implement fragmentation in future task */
        err = XGL_ERR_BUFFER_TOO_SMALL;
        goto unlock_and_return;
    }
    
    /* Convert zero-copy data to standard format */
    /* In zero-copy mode, we pass the pointer to the data portion */
    /* The buffer already has header space reserved */
    standard_tx_data.target_id = tx_data->target_id;
    standard_tx_data.data_type = tx_data->data_type;
    standard_tx_data.data = tx_data->buffer + tx_data->data_offset;
    standard_tx_data.data_len = tx_data->data_len;
    standard_tx_data.reliable = tx_data->reliable;
    standard_tx_data.priority = tx_data->priority;
    
    /* Create and send frame */
    err = create_and_send_frame(handle, &standard_tx_data);
    
    /* Update statistics */
    if (err == XGL_OK) {
        handle->stats.tx_packets++;
        handle->stats.tx_bytes += tx_data->data_len;
    } else {
        handle->stats.tx_errors++;
    }
    
unlock_and_return:
#ifdef XGL_THREAD_SAFE
    /* Unlock mutex if thread safety is enabled */
    if (handle->config.thread_safe) {
        xgl_mutex_unlock(&handle->mutex);
    }
#endif
    
    return err;
}

