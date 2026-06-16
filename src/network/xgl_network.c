/**
 * \file            xgl_network.c
 * \brief           Network layer implementation
 * \author          X-Gen Lab
 */

#include "xgl_network_internal.h"
#include <xgl/xgl_error.h>
#include <xgl/internal/xgl_wire.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Private Helper Functions                                                  */
/*---------------------------------------------------------------------------*/

static xgl_error_t network_find_security_ext(const uint8_t* frame_buf,
                                             const xgl_wire_header_t* header,
                                             uint32_t* key_id,
                                             uint8_t* tag_len) {
    if (frame_buf == NULL || header == NULL || key_id == NULL || tag_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *key_id = 0U;
    *tag_len = 0U;
    if (header->header_len <= XGL_WIRE_BASE_HEADER_SIZE) {
        return XGL_ERR_NOT_FOUND;
    }

    xgl_wire_ext_cursor_t cursor;
    xgl_error_t err = xgl_wire_ext_cursor_init(
        &cursor,
        &frame_buf[XGL_WIRE_BASE_HEADER_SIZE],
        header->header_len - XGL_WIRE_BASE_HEADER_SIZE
    );
    if (err != XGL_OK) {
        return err;
    }

    xgl_wire_ext_t ext;
    while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
        if (ext.type != XGL_WIRE_EXT_SECURITY) {
            continue;
        }

        uint64_t nonce_id = 0U;
        return xgl_wire_decode_security_ext_value(ext.value,
                                                  ext.len,
                                                  key_id,
                                                  &nonce_id,
                                                  tag_len);
    }

    return err;
}

xgl_error_t network_resign_forwarded_frame(xgl_network_ctx_t* ctx,
                                           uint8_t* frame_buf,
                                           size_t frame_len,
                                           const xgl_wire_header_t* header) {
    if (ctx == NULL || frame_buf == NULL || header == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    bool authenticated = (header->flags & XGL_WIRE_FLAG_AUTHENTICATED) != 0U;
    if (!authenticated) {
        return ctx->auth_required ? XGL_ERR_INVALID_FRAME : XGL_OK;
    }

    if (ctx->auth_provider == NULL || ctx->auth_provider->sign == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    uint32_t key_id = 0U;
    uint8_t tag_len = 0U;
    xgl_error_t err = network_find_security_ext(frame_buf, header, &key_id, &tag_len);
    if (err != XGL_OK || tag_len == 0U || tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (ctx->auth_required && key_id != ctx->auth_key_id) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t tag_offset = (size_t)header->header_len + (size_t)header->payload_len;
    if (frame_len < tag_offset + (size_t)tag_len + XGL_CRC16_SIZE ||
        tag_offset + (size_t)tag_len != frame_len - XGL_CRC16_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }

    uint8_t tag[XGL_AUTH_TAG_MAX_LEN] = {0};
    size_t produced_tag_len = 0U;
    err = ctx->auth_provider->sign(key_id,
                                   frame_buf,
                                   header->header_len,
                                   &frame_buf[header->header_len],
                                   header->payload_len,
                                   tag,
                                   sizeof(tag),
                                   &produced_tag_len,
                                   ctx->auth_provider->user_data);
    if (err != XGL_OK) {
        return err;
    }

    if (produced_tag_len != tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }

    memcpy(&frame_buf[tag_offset], tag, produced_tag_len);
    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Public API Implementation                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize network layer context
 */
xgl_error_t xgl_network_init(xgl_network_ctx_t* ctx,
                             const xgl_network_config_t* config) {
    if (ctx == NULL || config == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (config->route_table == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_network_ctx_t));
    ctx->local_id = config->local_id;
    ctx->route_table = config->route_table;
    ctx->upper_layer = config->upper_layer;
    ctx->lower_layer = config->lower_layer;
    ctx->error_callback = config->error_callback;
    ctx->callback_user_data = config->callback_user_data;
    ctx->stats = config->stats;
    ctx->auth_required = config->auth_required;
    ctx->auth_key_id = config->auth_key_id;
    ctx->auth_provider = config->auth_provider;

    return XGL_OK;
}

/**
 * \brief           Validate packet addressing
 * \details         Checks if source and target IDs are valid
 */
bool xgl_network_validate_address(const xgl_network_ctx_t* ctx,
                                  uint16_t target_id,
                                  uint16_t source_id) {
    if (ctx == NULL) {
        return false;
    }

    /* Source ID should not be broadcast */
    if (source_id == XGL_BROADCAST_ID) {
        return false;
    }

    /* Source ID should not be zero (reserved) */
    if (source_id == 0) {
        return false;
    }

    /* Target ID can be broadcast or specific node */
    /* Zero is reserved but we allow it for special cases */

    if (source_id == target_id && target_id != XGL_BROADCAST_ID) {
        return false;
    }

    return true;
}

/**
 * \brief           Invoke error callback
 * \details         Reports error through registered callback
 */
void xgl_network_report_error(xgl_network_ctx_t* ctx,
                              xgl_handle_t handle,
                              xgl_error_t error,
                              const char* message) {
    if (ctx == NULL) {
        return;
    }

    if (ctx->error_callback != NULL) {
        ctx->error_callback(handle, error, message, ctx->callback_user_data);
    }

    /* Update error statistics */
    if (ctx->stats != NULL) {
        ctx->stats->tx_errors++;
    }
}

/*---------------------------------------------------------------------------*/
/* Layer Interface Implementation                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Network layer send implementation (called by upper layers)
 * \details         This function is called by transport layer to send packets
 */
static xgl_error_t network_send_impl(void* ctx,
                                    xgl_handle_t handle,
                                    void* data) {
    xgl_network_ctx_t* net_ctx = (xgl_network_ctx_t*)ctx;
    xgl_packet_t* packet = (xgl_packet_t*)data;

    if (net_ctx == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Forward to network send function */
    return xgl_network_send_with_handle(net_ctx, handle, packet, false);
}

/**
 * \brief           Network layer receive implementation (called by lower layers)
 * \details         This function is called by datalink layer to deliver frames
 */
static xgl_error_t network_receive_impl(void* ctx,
                                       xgl_handle_t handle,
                                       // cppcheck-suppress constParameterCallback
                                       void* data) {
    xgl_network_ctx_t* net_ctx = (xgl_network_ctx_t*)ctx;

    if (net_ctx == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Extract frame buffer and length from data */
    const xgl_frame_rx_message_t* frame_data = (const xgl_frame_rx_message_t*)data;

    /* Forward to network receive function */
    return xgl_network_receive(net_ctx, handle, frame_data->frame_buf, frame_data->frame_len);
}

/**
 * \brief           Network layer error reporting implementation
 * \details         This function is called to report errors to upper layers
 */
static xgl_error_t network_report_error_impl(void* ctx,
                                            xgl_handle_t handle,
                                            void* data) {
    xgl_network_ctx_t* net_ctx = (xgl_network_ctx_t*)ctx;
    xgl_layer_error_info_t* error_info = (xgl_layer_error_info_t*)data;

    if (net_ctx == NULL || error_info == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Forward error to callback if available */
    if (net_ctx->error_callback != NULL) {
        net_ctx->error_callback(handle, error_info->error,
                               error_info->message,
                               net_ctx->callback_user_data);
    }

    return XGL_OK;
}

/**
 * \brief           Get network layer interface
 * \details         Returns the layer interface for this network instance
 * \param[in]       ctx: Network layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_network_get_interface(xgl_network_ctx_t* ctx,
                                     xgl_layer_interface_t* iface) {
    if (ctx == NULL || iface == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_layer_interface_init(iface,
                            ctx,
                            network_send_impl,
                            network_receive_impl,
                            network_report_error_impl);

    return XGL_OK;
}
