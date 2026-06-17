/**
 * \file            xgl_reliable_extensions.c
 * \brief           Reliable packet extension storage
 * \author          X-Gen Lab
 */

#include <string.h>

#include "xgl_reliable_internal.h"

xgl_error_t xgl_reliable_set_packet_extensions(xgl_reliable_queue_t *queue,
                                               xgl_reliable_packet_t *packet,
                                               const uint8_t *extensions,
                                               size_t extensions_len)
{
    if (queue == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (extensions == NULL && extensions_len > 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (packet->extensions != NULL) {
        reliable_free(queue->allocator, packet->extensions);
        packet->extensions = NULL;
        packet->extensions_len = 0U;
    }

    if (extensions_len == 0U) {
        return XGL_OK;
    }

    packet->extensions =
        (uint8_t *) reliable_malloc(queue->allocator, extensions_len);
    if (packet->extensions == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    memcpy(packet->extensions, extensions, extensions_len);
    packet->extensions_len = extensions_len;
    return XGL_OK;
}
