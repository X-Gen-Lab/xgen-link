/**
 * \file            xgl_reliable.c
 * \brief           Owned reliable records, index and retry backoff
 */

#include <xgen/memory/allocator.h>

#include <string.h>
#include <transport/xgl_reliable.h>

static size_t reliable_index_bucket(uint16_t target_id,
                                    uint32_t packet_number) {
    uint32_t mixed = packet_number ^ ((uint32_t)target_id * 2654435761UL);
    return (size_t)(mixed % XGL_RELIABLE_INDEX_BUCKETS);
}

static void reliable_index_packet(xgl_reliable_queue_t* queue,
                                  xgl_reliable_packet_t* packet) {
    if (queue == NULL || packet == NULL) {
        return;
    }

    size_t bucket =
        reliable_index_bucket(packet->target_id, packet->packet_number);
    packet->index_next = queue->index_buckets[bucket];
    queue->index_buckets[bucket] = packet;
}

static void reliable_unindex_packet(xgl_reliable_queue_t* queue,
                                    xgl_reliable_packet_t* packet) {
    if (queue == NULL || packet == NULL) {
        return;
    }

    size_t bucket =
        reliable_index_bucket(packet->target_id, packet->packet_number);
    xgl_reliable_packet_t* previous = NULL;
    xgl_reliable_packet_t* current = queue->index_buckets[bucket];
    while (current != NULL) {
        if (current == packet) {
            if (previous == NULL) {
                queue->index_buckets[bucket] = current->index_next;
            } else {
                previous->index_next = current->index_next;
            }
            current->index_next = NULL;
            return;
        }
        previous = current;
        current = current->index_next;
    }
}

static void reliable_free_packet(const xgl_reliable_queue_t* queue,
                                 xgl_reliable_packet_t* packet) {
    if (packet == NULL) {
        return;
    }

    if (packet->data != NULL) {
        xgm_free(queue->data_allocator, packet->data);
        packet->data = NULL;
    }

    if (packet->extensions != NULL) {
        xgm_free(queue->extensions_allocator, packet->extensions);
        packet->extensions = NULL;
    }

    xgm_free(queue->allocator, packet);
}

xgl_reliable_packet_t*
xgl_reliable_find_packet_number(const xgl_reliable_queue_t* queue,
                                uint32_t packet_number, uint16_t target_id) {
    if (queue == NULL) {
        return NULL;
    }

    size_t bucket = reliable_index_bucket(target_id, packet_number);
    xgl_reliable_packet_t* packet = queue->index_buckets[bucket];
    while (packet != NULL) {
        if (packet->packet_number == packet_number &&
            packet->target_id == target_id) {
            return packet;
        }
        packet = packet->index_next;
    }

    return NULL;
}

xgl_error_t xgl_reliable_set_packet_extensions(
    const xgl_reliable_queue_t* queue, xgl_reliable_packet_t* packet,
    const uint8_t* extensions, size_t extensions_len) {
    if (queue == NULL || packet == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (extensions == NULL && extensions_len > 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (packet->extensions != NULL) {
        xgm_free(queue->extensions_allocator, packet->extensions);
        packet->extensions = NULL;
        packet->extensions_len = 0U;
    }

    if (extensions_len == 0U) {
        return XGL_OK;
    }

    packet->extensions =
        (uint8_t*)xgm_alloc(queue->extensions_allocator, extensions_len);
    if (packet->extensions == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    memcpy(packet->extensions, extensions, extensions_len);
    packet->extensions_len = extensions_len;
    return XGL_OK;
}

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

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
xgl_error_t xgl_reliable_add_packet_number(
    xgl_reliable_queue_t* queue, const uint8_t* data, size_t data_len,
    uint16_t source_id, uint16_t target_id, uint32_t packet_number,
    uint8_t data_type, uint8_t priority, int32_t timeout_ms,
    xgl_reliable_packet_t** packet_out) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (packet_out != NULL) {
        *packet_out = NULL;
    }
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
    if (packet_out != NULL) {
        *packet_out = packet;
    }

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
            /* Intrusive node membership is established by the owning list. */
            /* NOLINTNEXTLINE(bugprone-casting-through-void) */
            XGCT_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        reliable_unindex_packet(queue, packet);
        reliable_free_packet(queue, packet);
    }

    memset((void*)queue->index_buckets, 0, sizeof(queue->index_buckets));
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Calculate exponential backoff without signed overflow
 * \param[in]       initial_timeout_ms: Initial timeout; nonpositive means zero
 * \param[in]       retry_count: Retry exponent, limited to ten
 * \return          Timeout saturated to the inclusive range 0 to 30000 ms
 */
int32_t xgl_reliable_calc_backoff(int32_t initial_timeout_ms,
                                  uint8_t retry_count) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    const int32_t maximum_timeout_ms = 30000;
    if (initial_timeout_ms <= 0) {
        return 0;
    }
    if (initial_timeout_ms >= maximum_timeout_ms) {
        return maximum_timeout_ms;
    }

    int32_t backoff = initial_timeout_ms;
    /* Preserve the bounded exponent used by the reliable queue helpers. */
    if (retry_count > 10) {
        retry_count = 10;
    }

    for (uint8_t i = 0; i < retry_count; i++) {
        /* Check before multiplication, including large valid initial values. */
        if (backoff >= maximum_timeout_ms / 2) {
            return maximum_timeout_ms;
        }
        backoff *= 2;
    }

    return backoff;
}
