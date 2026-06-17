/**
 * \file            xgl_reliable.c
 * \brief           Reliable Transmission Queue Implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_reliable.h>

#include <string.h>

#include "xgl_reliable_internal.h"

/*---------------------------------------------------------------------------*/
/* Reliable Queue Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize reliable transmission queue
 */
xgl_error_t xgl_reliable_init(xgl_reliable_queue_t *queue,
                              uint8_t max_retry_count,
                              xgl_allocator_t *allocator)
{
    if (queue == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    memset(queue, 0, sizeof(*queue));

    /* Initialize wait-ACK list */
    xgl_list_init(&queue->wait_ack_list);

    /* Store configuration */
    queue->max_retry_count = max_retry_count;
    queue->allocator = allocator;

    return XGL_OK;
}

/**
 * \brief           Destroy reliable transmission queue
 */
void xgl_reliable_destroy(xgl_reliable_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }

    /* Clear all packets */
    xgl_reliable_clear(queue);
}

xgl_error_t xgl_reliable_add_packet_number(
    xgl_reliable_queue_t *queue, const uint8_t *data, size_t data_len,
    uint16_t source_id, uint16_t target_id, uint32_t packet_number,
    uint8_t data_type, uint8_t priority, int32_t timeout_ms, xgl_phy_ops_t *phy)
{
    if (queue == NULL || data == NULL || data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }

    /* Note: phy can be NULL in layered architecture - retransmission handled by
     * network layer */

    /* Allocate packet structure */
    xgl_reliable_packet_t *packet = (xgl_reliable_packet_t *) reliable_malloc(
        queue->allocator, sizeof(xgl_reliable_packet_t));

    if (packet == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(packet, 0, sizeof(*packet));

    /* Allocate data buffer */
    packet->data = (uint8_t *) reliable_malloc(queue->allocator, data_len);
    if (packet->data == NULL) {
        reliable_free(queue->allocator, packet);
        return XGL_ERR_NO_MEMORY;
    }

    /* Copy packet data */
    memcpy(packet->data, data, data_len);
    packet->data_len = data_len;

    /* Set addressing */
    packet->source_id = source_id;
    packet->target_id = target_id;
    packet->packet_number = packet_number;
    packet->data_type = data_type;
    packet->packet_type = XGL_PACKET_TYPE_DATA;

    /* Set attributes */
    packet->priority = priority;

    /* Initialize retransmission state */
    packet->retry_count = 0;
    packet->send_timestamp = 0; /* Will be set on first transmission */
    packet->timeout_ms = timeout_ms;
    packet->initial_timeout_ms = timeout_ms;

    /* Set routing */
    packet->phy = phy;

    /* Initialize list node */
    xgl_list_node_init(&packet->node);

    /* Add to wait-ACK list */
    xgl_list_insert_tail(&queue->wait_ack_list, &packet->node);
    reliable_index_packet(queue, packet);

    return XGL_OK;
}

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

xgl_error_t xgl_reliable_remove_packet_number(xgl_reliable_queue_t *queue,
                                              uint32_t packet_number,
                                              uint16_t target_id)
{
    if (queue == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_reliable_packet_t *packet =
        xgl_reliable_find_packet_number(queue, packet_number, target_id);
    if (packet != NULL) {
        reliable_unindex_packet(queue, packet);
        xgl_list_remove(&queue->wait_ack_list, &packet->node);
        reliable_free_packet(queue, packet);
        return XGL_OK;
    }

    return XGL_ERR_SEQUENCE_ERROR; /* Packet not found */
}

/**
 * \brief           Get number of packets in wait-ACK queue
 */
size_t xgl_reliable_get_count(const xgl_reliable_queue_t *queue)
{
    if (queue == NULL) {
        return 0;
    }

    return xgl_list_count(&queue->wait_ack_list);
}

/**
 * \brief           Check if queue is empty
 */
bool xgl_reliable_is_empty(const xgl_reliable_queue_t *queue)
{
    if (queue == NULL) {
        return true;
    }

    return xgl_list_is_empty(&queue->wait_ack_list);
}

/**
 * \brief           Clear all packets from queue
 */
void xgl_reliable_clear(xgl_reliable_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }

    /* Remove and free all packets */
    xgl_list_node_t *node;
    while ((node = xgl_list_remove_head(&queue->wait_ack_list)) != NULL) {
        xgl_reliable_packet_t *packet =
            XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        reliable_unindex_packet(queue, packet);
        reliable_free_packet(queue, packet);
    }

    memset(queue->index_buckets, 0, sizeof(queue->index_buckets));
}
