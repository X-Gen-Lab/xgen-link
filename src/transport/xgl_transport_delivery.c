/**
 * \file            xgl_transport_delivery.c
 * \brief           Transport payload delivery helpers
 */

#include "xgl/internal/xgl_wire.h"
#include "xgl_transport_internal.h"

xgl_error_t transport_deliver_packet(xgl_transport_ctx_t *ctx,
                                     xgl_handle_t handle,
                                     const xgl_packet_t *packet,
                                     const uint8_t *data, size_t data_len)
{
    if (ctx == NULL || packet == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    uint16_t source_id = packet->source_id;
    uint8_t data_type = packet->data_type;

    if (packet->fragment && ctx->fragment_mgr != NULL) {
        uint8_t *complete_data = NULL;
        size_t complete_len = 0;
        uint32_t message_id = 0U;
        uint32_t fragment_offset = 0U;
        uint32_t message_len = 0U;
        bool has_fragment_ext = false;

        if (packet->extensions != NULL && packet->extensions_len > 0U) {
            xgl_wire_ext_cursor_t cursor;
            xgl_error_t err = xgl_wire_ext_cursor_init(
                &cursor, packet->extensions, packet->extensions_len);
            if (err != XGL_OK) {
                return err;
            }

            xgl_wire_ext_t ext;
            while ((err = xgl_wire_ext_cursor_next(&cursor, &ext)) == XGL_OK) {
                if (ext.type == XGL_WIRE_EXT_FRAGMENT) {
                    err = xgl_wire_decode_fragment_ext_value(
                        ext.value, ext.len, &message_id, &fragment_offset,
                        &message_len);
                    if (err != XGL_OK) {
                        return err;
                    }
                    has_fragment_ext = true;
                    break;
                }
            }
            if (err != XGL_OK && err != XGL_ERR_NOT_FOUND) {
                return err;
            }
        }

        if (!has_fragment_ext) {
            return XGL_ERR_INVALID_FRAME;
        }

        xgl_error_t err = xgl_fragment_process_ext(
            ctx->fragment_mgr, source_id, packet->connection_id,
            packet->session_epoch, data_type, message_id, fragment_offset,
            message_len, data, data_len, &complete_data, &complete_len, 0);

        if (err == XGL_OK) {
            if (ctx->rx_callback != NULL) {
                ctx->rx_callback(handle, source_id, data_type, complete_data,
                                 complete_len, ctx->callback_user_data);
            }
            xgl_fragment_free_data(ctx->fragment_mgr, complete_data);
            if (ctx->stats != NULL) {
                ctx->stats->rx_packets++;
                ctx->stats->rx_bytes += complete_len;
            }
        } else if (err == XGL_ERR_BUSY) {
            return XGL_OK;
        } else {
            return err;
        }
    } else {
        if (ctx->rx_callback != NULL) {
            ctx->rx_callback(handle, source_id, data_type, data, data_len,
                             ctx->callback_user_data);
        }

        if (ctx->stats != NULL) {
            ctx->stats->rx_packets++;
            ctx->stats->rx_bytes += data_len;
        }
    }

    return XGL_OK;
}
