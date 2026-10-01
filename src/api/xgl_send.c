/**
 * \file            xgl_send.c
 * \brief           Send API implementation
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>

#include "xgl_instance_internal.h"

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

/*---------------------------------------------------------------------------*/
/* Standard Send API (with copy)                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Send data (standard mode with copy)
 * \details         Copies data internally and sends through transport layer
 */
xgl_error_t xgl_send_at(xgl_handle_t handle, const xgl_tx_data_t* tx_data,
                        uint32_t now_ms) {
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

    /* Send through transport layer */
    handle->layers.transport_ctx.current_time_ms = now_ms;
    err = xgl_transport_send(&handle->layers.transport_ctx, handle, tx_data);

    return err;
}
