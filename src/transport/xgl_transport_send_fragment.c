/**
 * \file            xgl_transport_send_fragment.c
 * \brief           Transport fragmented send path implementation
 */

#include "xgl_transport_send_internal.h"
#include "xgl/internal/xgl_wire.h"

static xgl_error_t transport_encode_fragment_ext(uint32_t message_id,
                                                 uint32_t fragment_offset,
                                                 uint32_t total_len,
                                                 uint8_t* fragment_ext,
                                                 size_t fragment_ext_capacity,
                                                 size_t* encoded_ext_len) {
    uint8_t fragment_ext_value[XGL_FRAGMENT_EXT_VALUE_SIZE] = {0};
    size_t fragment_ext_value_len = 0U;
    xgl_error_t err = xgl_wire_encode_fragment_ext_value(fragment_ext_value,
                                                         sizeof(fragment_ext_value),
                                                         message_id,
                                                         fragment_offset,
                                                         total_len,
                                                         &fragment_ext_value_len);
    if (err != XGL_OK) {
        return err;
    }

    return xgl_wire_encode_ext(fragment_ext,
                               fragment_ext_capacity,
                               XGL_WIRE_EXT_FRAGMENT,
                               fragment_ext_value,
                               fragment_ext_value_len,
                               encoded_ext_len);
}

xgl_error_t transport_send_fragmented(xgl_transport_ctx_t* ctx,
                                      xgl_handle_t handle,
                                      xgl_transport_peer_state_t* peer,
                                      const xgl_tx_data_t* tx_data,
                                      const xgl_transport_send_plan_t* plan) {
    if (ctx->fragment_mgr == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    size_t fragment_payload_max = plan->fragment_payload_budget;
    uint32_t message_id = ctx->fragment_mgr->next_message_id++;

    for (size_t i = 0U; i < plan->fragment_count; i++) {
        size_t fragment_offset = i * fragment_payload_max;
        size_t remaining = tx_data->data_len - fragment_offset;
        size_t fragment_payload_len =
            (remaining < fragment_payload_max) ? remaining : fragment_payload_max;

        if (tx_data->reliable && peer != NULL &&
            !xgl_window_can_send_packet_number(&peer->tx_window)) {
            return XGL_ERR_WINDOW_FULL;
        }

        uint32_t packet_number = 0U;
        if (tx_data->reliable && peer != NULL) {
            packet_number = xgl_window_get_next_packet_number(&peer->tx_window);
        }

        uint8_t fragment_ext[XGL_FRAGMENT_EXT_SIZE] = {0};
        size_t encoded_ext_len = 0U;
        xgl_error_t err = transport_encode_fragment_ext(message_id,
                                                        (uint32_t)fragment_offset,
                                                        (uint32_t)tx_data->data_len,
                                                        fragment_ext,
                                                        sizeof(fragment_ext),
                                                        &encoded_ext_len);
        if (err != XGL_OK) {
            return err;
        }

        xgl_reliable_packet_t* rel_packet = NULL;
        err = transport_queue_reliable_tx(ctx,
                                          peer,
                                          tx_data,
                                          &tx_data->data[fragment_offset],
                                          fragment_payload_len,
                                          packet_number,
                                          true,
                                          fragment_ext,
                                          encoded_ext_len,
                                          &rel_packet);
        if (err != XGL_OK) {
            return err;
        }

        err = transport_send_packet_view(ctx,
                                         handle,
                                         peer,
                                         tx_data,
                                         &tx_data->data[fragment_offset],
                                         fragment_payload_len,
                                         packet_number,
                                         true,
                                         fragment_ext,
                                         encoded_ext_len,
                                         &rel_packet);
        if (err != XGL_OK) {
            return err;
        }
    }

    return XGL_OK;
}
