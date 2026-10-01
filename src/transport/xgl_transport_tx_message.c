/**
 * \file            xgl_transport_tx_message.c
 * \brief           Owned messages advanced through the transmit window
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_send_internal.h"
#include <string.h>

/**
 * \brief           Encode a message fragment extension
 * \param[in]       message_id: Message identity shared by every fragment
 * \param[in]       fragment_offset: Byte offset within the message
 * \param[in]       total_len: Total message length
 * \param[out]      fragment_ext: Encoded extension buffer
 * \param[in]       fragment_ext_capacity: Extension buffer capacity
 * \param[out]      encoded_ext_len: Encoded extension length
 * \return          XGL_OK or an encoding error
 */
static xgl_error_t transport_encode_fragment_ext(uint32_t message_id,
                                                 uint32_t fragment_offset,
                                                 uint32_t total_len,
                                                 uint8_t* fragment_ext,
                                                 size_t fragment_ext_capacity,
                                                 size_t* encoded_ext_len) {
    uint8_t value[XGL_FRAGMENT_EXT_VALUE_SIZE] = {0};
    size_t value_len = 0U;
    xgl_error_t err = xgl_wire_encode_fragment_ext_value(
        value, sizeof(value), message_id, fragment_offset, total_len,
        &value_len);
    if (err != XGL_OK) {
        return err;
    }
    return xgl_wire_encode_ext(fragment_ext, fragment_ext_capacity,
                               XGL_WIRE_EXT_FRAGMENT, value, value_len,
                               encoded_ext_len);
}

/**
 * \brief           Queue and synchronously transmit one message fragment
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Reliable owner, or NULL for unreliable transmission
 * \param[in]       tx_data: Message metadata and bytes
 * \param[in]       message_id: Fragment message identity
 * \param[in]       offset: Next message byte to transmit
 * \param[in]       length: Fragment payload size
 * \return          XGL_OK only after the lower layer has accepted the bytes
 */
static xgl_error_t transport_send_message_fragment(
    xgl_transport_ctx_t* ctx, xgl_handle_t handle,
    xgl_transport_peer_state_t* peer, const xgl_tx_data_t* tx_data,
    uint32_t message_id, size_t offset, size_t length) {
    uint8_t extension[XGL_FRAGMENT_EXT_SIZE] = {0};
    size_t extension_len = 0U;
    xgl_error_t err = transport_encode_fragment_ext(
        message_id, (uint32_t)offset, (uint32_t)tx_data->data_len, extension,
        sizeof(extension), &extension_len);
    if (err != XGL_OK) {
        return err;
    }

    uint32_t packet_number =
        peer != NULL ? xgl_window_get_next_packet_number(&peer->tx_window) : 0U;
    xgl_reliable_packet_t* packet = NULL;
    err = transport_queue_reliable_tx(
        ctx, peer, tx_data, tx_data->data + offset, length, packet_number, true,
        extension, extension_len, &packet);
    if (err != XGL_OK) {
        return err;
    }
    return transport_send_packet_view(
        ctx, handle, peer, tx_data, tx_data->data + offset, length,
        packet_number, true, extension, extension_len, &packet);
}

/**
 * \brief           Release the unsent message copy on completion, reset or
 *                  failure
 * \param[in,out]   ctx: Transport context
 * \param[in,out]   peer: Message owner
 */
void transport_clear_tx_message(xgl_transport_ctx_t* ctx,
                                xgl_transport_peer_state_t* peer) {
    ctx->tx_message_bytes -= peer->tx_message.data_len;
    xgm_free(ctx->memory.tx_message, peer->tx_message_storage);
    peer->tx_message_storage = NULL;
    memset(&peer->tx_message, 0, sizeof(peer->tx_message));
    peer->tx_message_offset = 0U;
    peer->tx_fragment_payload_size = 0U;
    peer->tx_message_retry_pending = false;
}

/**
 * \brief           Fill available peer window slots from an admitted message
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Message owner
 * \return          XGL_OK while accepted work remains valid; hard failures fail
 *                  peer
 * \note            Temporary allocation or PHY backpressure retains unsent
 *                  bytes.
 */
xgl_error_t transport_pump_tx_message(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle,
                                      xgl_transport_peer_state_t* peer) {
    if (peer->failed) {
        return XGL_ERR_ACK_TIMEOUT;
    }
    if (peer->tx_message_retry_pending) {
        if (transport_now(ctx) - peer->tx_message_retry_started_ms <
            transport_retry_delay_ms(ctx)) {
            return XGL_OK;
        }
        peer->tx_message_retry_pending = false;
    }
    while (peer->tx_message.data != NULL &&
           xgl_window_can_send_packet_number(&peer->tx_window)) {
        size_t remaining = peer->tx_message.data_len - peer->tx_message_offset;
        size_t length = remaining < peer->tx_fragment_payload_size
                            ? remaining
                            : peer->tx_fragment_payload_size;
        xgl_error_t err = transport_send_message_fragment(
            ctx, handle, peer, &peer->tx_message, peer->tx_message_id,
            peer->tx_message_offset, length);
        if (err == XGL_ERR_BUSY || err == XGL_ERR_NO_MEMORY ||
            err == XGL_ERR_WINDOW_FULL) {
            peer->tx_message_retry_pending = true;
            peer->tx_message_retry_started_ms = transport_now(ctx);
            return XGL_OK;
        }
        if (err != XGL_OK) {
            transport_fail_peer(ctx, handle, peer, err);
            return err;
        }
        peer->tx_message_offset += length;
        if (peer->tx_message_offset == peer->tx_message.data_len) {
            /* Every remaining byte now has a reliable packet owner. */
            transport_clear_tx_message(ctx, peer);
        }
    }
    return XGL_OK;
}

/**
 * \brief           Admit one bounded reliable message or send unreliable
 *                  fragments
 * \param[in,out]   ctx: Transport context
 * \param[in]       handle: Protocol instance handle
 * \param[in,out]   peer: Reliable message owner, or NULL for unreliable sending
 * \param[in]       tx_data: Borrowed message data; copied before reliable
 *                  admission
 * \param[in]       plan: Validated per-frame payload budget
 * \return          XGL_OK means reliable message admission, not remote delivery
 */
xgl_error_t transport_send_fragmented(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle,
                                      xgl_transport_peer_state_t* peer,
                                      const xgl_tx_data_t* tx_data,
                                      const xgl_transport_send_plan_t* plan) {
    if (ctx->fragment_mgr == NULL || plan->fragment_payload_budget == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (tx_data->compression_id != 0U) {
        return XGL_ERR_UNSUPPORTED;
    }
    if (tx_data->data_len > ctx->max_message_size) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
#if SIZE_MAX > UINT32_MAX
    if (tx_data->data_len > UINT32_MAX) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
#endif

    if (!tx_data->reliable) {
        uint32_t message_id = ctx->next_message_id++;
        for (size_t offset = 0U; offset < tx_data->data_len;) {
            size_t remaining = tx_data->data_len - offset;
            size_t length = remaining < plan->fragment_payload_budget
                                ? remaining
                                : plan->fragment_payload_budget;
            xgl_error_t err = transport_send_message_fragment(
                ctx, handle, NULL, tx_data, message_id, offset, length);
            if (err != XGL_OK) {
                return err;
            }
            offset += length;
        }
        return XGL_OK;
    }

    if (peer == NULL || peer->tx_message.data != NULL) {
        return XGL_ERR_BUSY;
    }
    if (plan->fragment_count >
        UINT32_MAX - peer->tx_window.next_packet_number) {
        return XGL_ERR_SEQUENCE_ERROR;
    }
    if (tx_data->data_len > ctx->max_tx_message_bytes - ctx->tx_message_bytes) {
        return XGL_ERR_NO_MEMORY;
    }
    uint8_t* owned_data = xgm_alloc(ctx->memory.tx_message, tx_data->data_len);
    if (owned_data == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memcpy(owned_data, tx_data->data, tx_data->data_len);
    ctx->tx_message_bytes += tx_data->data_len;
    peer->tx_message_storage = owned_data;
    peer->tx_message = *tx_data;
    peer->tx_message.data = owned_data;
    peer->tx_message_offset = 0U;
    peer->tx_fragment_payload_size = plan->fragment_payload_budget;
    peer->tx_message_id = ctx->next_message_id++;

    /* Admission has completed. Later failures use the peer error callback. */
    (void)transport_pump_tx_message(ctx, handle, peer);
    return XGL_OK;
}
