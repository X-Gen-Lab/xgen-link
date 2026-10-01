/**
 * \file            xgl_transport_memory.c
 * \brief           Transport allocator helpers
 */

#include "xgen/memory/allocator.h"
#include "xgl_transport_internal.h"

#if XGL_FEATURE_OUT_OF_ORDER
/**
 * \brief           Release a buffered receive packet through its resource
 * services
 * \param[in]       ctx: Transport context
 * \param[in,out]   buffered: Owned packet storage, or NULL
 */
void transport_free_rx_buffered_packet(
    const xgl_transport_ctx_t* ctx,
    xgl_transport_rx_buffered_packet_t* buffered) {
    if (buffered == NULL) {
        return;
    }

    xgm_free(ctx->memory.rx_payload, buffered->data);
    xgm_free(ctx->memory.rx_extensions, buffered->extensions);
    xgm_free(ctx->memory.rx_packet, buffered);
}

#endif
