/**
 * \file            xgl_transport_memory.c
 * \brief           Transport allocator helpers
 */

#include "xgl/internal/xgl_allocator.h"
#include "xgl_transport_internal.h"

void *transport_malloc(xgl_allocator_t *allocator, size_t size)
{
    return xgl_alloc(allocator, size);
}

void transport_free(xgl_allocator_t *allocator, void *ptr)
{
    xgl_free(allocator, ptr);
}

void transport_free_rx_buffered_packet(
    xgl_transport_ctx_t *ctx, xgl_transport_rx_buffered_packet_t *buffered)
{
    if (buffered == NULL) {
        return;
    }

    transport_free(ctx != NULL ? ctx->allocator : NULL, buffered->data);
    transport_free(ctx != NULL ? ctx->allocator : NULL, buffered->extensions);
    transport_free(ctx != NULL ? ctx->allocator : NULL, buffered);
}
