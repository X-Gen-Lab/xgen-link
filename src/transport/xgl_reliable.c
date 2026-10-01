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
xgl_error_t xgl_reliable_init(xgl_reliable_queue_t* queue,
                              const xgm_allocator_t* allocator) {
    if (queue == NULL || !xgm_allocator_is_valid(allocator)) {
        return XGL_ERR_NULL_POINTER;
    }

    memset(queue, 0, sizeof(*queue));

    /* Initialize wait-ACK list */
    xgct_list_init(&queue->wait_ack_list);

    /* Store configuration */
    queue->allocator = allocator;
    queue->data_allocator = allocator;
    queue->extensions_allocator = allocator;

    return XGL_OK;
}

/**
 * \brief           Destroy reliable transmission queue
 */
void xgl_reliable_destroy(xgl_reliable_queue_t* queue) {
    if (queue == NULL) {
        return;
    }

    /* Clear all packets */
    xgl_reliable_clear(queue);
}

xgl_error_t xgl_reliable_add_packet_number(
    xgl_reliable_queue_t* queue, const uint8_t* data, size_t data_len,
    uint16_t source_id, uint16_t target_id, uint32_t packet_number,
    uint8_t data_type, uint8_t priority, int32_t timeout_ms) {
    if (queue == NULL || data == NULL || data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }

    /* Allocate packet structure */
    xgl_reliable_packet_t* packet = (xgl_reliable_packet_t*)xgm_alloc(
        queue->allocator, sizeof(xgl_reliable_packet_t));

    if (packet == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(packet, 0, sizeof(*packet));

    /* Allocate data buffer */
    packet->data = (uint8_t*)xgm_alloc(queue->data_allocator, data_len);
    if (packet->data == NULL) {
        xgm_free(queue->allocator, packet);
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

    /* Initialize list node */
    xgct_list_node_init(&packet->node);

    /* Add to wait-ACK list */
    xgct_list_insert_tail(&queue->wait_ack_list, &packet->node);
    reliable_index_packet(queue, packet);

    return XGL_OK;
}

xgl_error_t xgl_reliable_remove_packet_number(xgl_reliable_queue_t* queue,
                                              uint32_t packet_number,
                                              uint16_t target_id) {
    if (queue == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_reliable_packet_t* packet =
        xgl_reliable_find_packet_number(queue, packet_number, target_id);
    if (packet != NULL) {
        reliable_unindex_packet(queue, packet);
        xgct_list_remove(&queue->wait_ack_list, &packet->node);
        reliable_free_packet(queue, packet);
        return XGL_OK;
    }

    return XGL_ERR_SEQUENCE_ERROR; /* Packet not found */
}

/**
 * \brief           Get number of packets in wait-ACK queue
 */
size_t xgl_reliable_get_count(const xgl_reliable_queue_t* queue) {
    if (queue == NULL) {
        return 0;
    }

    return xgct_list_count(&queue->wait_ack_list);
}

/**
 * \brief           Check if queue is empty
 */
bool xgl_reliable_is_empty(const xgl_reliable_queue_t* queue) {
    if (queue == NULL) {
        return true;
    }

    return xgct_list_is_empty(&queue->wait_ack_list);
}

/**
 * \brief           Clear all packets from queue
 */
void xgl_reliable_clear(xgl_reliable_queue_t* queue) {
    if (queue == NULL) {
        return;
    }

    /* Remove and free all packets */
    xgct_list_node_t* node;
    while ((node = xgct_list_remove_head(&queue->wait_ack_list)) != NULL) {
        xgl_reliable_packet_t* packet =
            XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        reliable_unindex_packet(queue, packet);
        reliable_free_packet(queue, packet);
    }

    memset((void*)queue->index_buckets, 0, sizeof(queue->index_buckets));
}
