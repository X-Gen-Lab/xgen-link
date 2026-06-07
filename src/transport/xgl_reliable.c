/**
 * \file            xgl_reliable.c
 * \brief           Reliable Transmission Queue Implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_reliable.h>
#include <xgl/xgl_allocator.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using the configured allocator policy
 */
static void* reliable_malloc(xgl_allocator_t* allocator, size_t size) {
    return xgl_alloc(allocator, size);
}

/**
 * \brief           Free memory using the configured allocator policy
 */
static void reliable_free(xgl_allocator_t* allocator, void* ptr) {
    xgl_free(allocator, ptr);
}

/**
 * \brief           Free reliable packet and its data
 */
static void free_reliable_packet(xgl_reliable_queue_t* queue,
                                xgl_reliable_packet_t* packet) {
    if (packet == NULL) {
        return;
    }
    
    /* Free packet data buffer */
    if (packet->data != NULL) {
        reliable_free(queue->allocator, packet->data);
        packet->data = NULL;
    }

    if (packet->extensions != NULL) {
        reliable_free(queue->allocator, packet->extensions);
        packet->extensions = NULL;
    }
    
    /* Free packet structure */
    reliable_free(queue->allocator, packet);
}

/*---------------------------------------------------------------------------*/
/* Reliable Queue Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize reliable transmission queue
 */
xgl_error_t xgl_reliable_init(xgl_reliable_queue_t* queue,
                              uint8_t max_retry_count,
                              xgl_allocator_t* allocator) {
    if (queue == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
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
void xgl_reliable_destroy(xgl_reliable_queue_t* queue) {
    if (queue == NULL) {
        return;
    }
    
    /* Clear all packets */
    xgl_reliable_clear(queue);
}

xgl_error_t xgl_reliable_add_packet_number(xgl_reliable_queue_t* queue,
                                           const uint8_t* data,
                                           size_t data_len,
                                           uint16_t source_id,
                                           uint16_t target_id,
                                           uint32_t packet_number,
                                           uint8_t data_type,
                                           uint8_t priority,
                                           int32_t timeout_ms,
                                           xgl_phy_ops_t* phy) {
    if (queue == NULL || data == NULL || data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Note: phy can be NULL in layered architecture - retransmission handled by network layer */
    
    /* Allocate packet structure */
    xgl_reliable_packet_t* packet = (xgl_reliable_packet_t*)
        reliable_malloc(queue->allocator, sizeof(xgl_reliable_packet_t));
    
    if (packet == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(packet, 0, sizeof(*packet));
    
    /* Allocate data buffer */
    packet->data = (uint8_t*)reliable_malloc(queue->allocator, data_len);
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
    packet->send_timestamp = 0;  /* Will be set on first transmission */
    packet->timeout_ms = timeout_ms;
    packet->initial_timeout_ms = timeout_ms;
    
    /* Set routing */
    packet->phy = phy;
    
    /* Initialize list node */
    xgl_list_node_init(&packet->node);
    
    /* Add to wait-ACK list */
    xgl_list_insert_tail(&queue->wait_ack_list, &packet->node);
    
    return XGL_OK;
}

xgl_error_t xgl_reliable_set_packet_extensions(xgl_reliable_queue_t* queue,
                                               xgl_reliable_packet_t* packet,
                                               const uint8_t* extensions,
                                               size_t extensions_len) {
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

    packet->extensions = (uint8_t*)reliable_malloc(queue->allocator, extensions_len);
    if (packet->extensions == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    memcpy(packet->extensions, extensions, extensions_len);
    packet->extensions_len = extensions_len;
    return XGL_OK;
}

xgl_error_t xgl_reliable_remove_packet_number(xgl_reliable_queue_t* queue,
                                              uint32_t packet_number,
                                              uint16_t target_id) {
    if (queue == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Find packet with matching packet number and target ID */
    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&queue->wait_ack_list, node) {
        xgl_reliable_packet_t* packet = XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        
        if (packet->packet_number == packet_number && packet->target_id == target_id) {
            /* Remove from list */
            xgl_list_remove(&queue->wait_ack_list, node);
            
            /* Free packet */
            free_reliable_packet(queue, packet);
            
            return XGL_OK;
        }
    }
    
    return XGL_ERR_SEQUENCE_ERROR;  /* Packet not found */
}

size_t xgl_reliable_remove_ack_ranges(xgl_reliable_queue_t* queue,
                                      uint16_t target_id,
                                      uint32_t largest_ack,
                                      const xgl_wire_ack_range_t* ranges,
                                      size_t range_count) {
    if (queue == NULL || (ranges == NULL && range_count > 0U)) {
        return 0U;
    }

    size_t removed = 0U;
    uint64_t next_high = largest_ack;

    for (size_t i = 0; i < range_count; ++i) {
        if (ranges[i].length == 0U) {
            continue;
        }

        uint64_t high = next_high;
        if (i > 0U) {
            uint64_t skip = (uint64_t)ranges[i].gap + 1U;
            if (high < skip) {
                break;
            }
            high -= skip;
        }

        uint64_t low = 0U;
        if (high + 1U > ranges[i].length) {
            low = high - (uint64_t)ranges[i].length + 1U;
        }

        for (uint64_t packet_number = high;; --packet_number) {
            if (xgl_reliable_remove_packet_number(queue,
                                                  (uint32_t)packet_number,
                                                  target_id) == XGL_OK) {
                removed++;
            }

            if (packet_number == low) {
                break;
            }
        }

        next_high = low;
    }

    return removed;
}

/**
 * \brief           Process timeouts and retransmit packets
 */
uint32_t xgl_reliable_process_timeouts(xgl_reliable_queue_t* queue,
                                       uint32_t current_time_ms,
                                       xgl_reliable_packet_t** retry_exhausted) {
    if (queue == NULL) {
        return 0;
    }
    
    uint32_t retransmit_count = 0;
    
    /* Clear retry exhausted output */
    if (retry_exhausted != NULL) {
        *retry_exhausted = NULL;
    }
    
    /* Iterate through wait-ACK list */
    xgl_list_node_t* node;
    xgl_list_node_t* tmp;
    XGL_LIST_FOR_EACH_SAFE(&queue->wait_ack_list, node, tmp) {
        xgl_reliable_packet_t* packet = XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        
        /* Skip if packet hasn't been sent yet */
        if (packet->send_timestamp == 0) {
            continue;
        }
        
        /* Calculate elapsed time */
        uint32_t elapsed_ms = current_time_ms - packet->send_timestamp;
        
        /* Check if timeout occurred */
        if (elapsed_ms >= (uint32_t)packet->timeout_ms) {
            /* Check if retry count exhausted */
            if (packet->retry_count >= queue->max_retry_count) {
                /* Remove from list */
                xgl_list_remove(&queue->wait_ack_list, node);
                
                /* Return packet to caller for error handling */
                if (retry_exhausted != NULL && *retry_exhausted == NULL) {
                    *retry_exhausted = packet;
                } else {
                    /* Free packet if caller doesn't want it */
                    free_reliable_packet(queue, packet);
                }
                
                continue;
            }
            
            /* Increment retry count */
            packet->retry_count++;
            
            /* Apply exponential backoff */
            packet->timeout_ms = xgl_reliable_calc_backoff(
                packet->initial_timeout_ms,
                packet->retry_count
            );
            
            /* Update send timestamp */
            packet->send_timestamp = current_time_ms;
            
            /* Retransmit packet */
            if (packet->phy != NULL && packet->phy->tx != NULL) {
                packet->phy->tx(packet->data, packet->data_len, packet->phy->user_data);
                retransmit_count++;
            }
        }
    }
    
    return retransmit_count;
}

/**
 * \brief           Get number of packets in wait-ACK queue
 */
size_t xgl_reliable_get_count(const xgl_reliable_queue_t* queue) {
    if (queue == NULL) {
        return 0;
    }
    
    return xgl_list_count(&queue->wait_ack_list);
}

/**
 * \brief           Check if queue is empty
 */
bool xgl_reliable_is_empty(const xgl_reliable_queue_t* queue) {
    if (queue == NULL) {
        return true;
    }
    
    return xgl_list_is_empty(&queue->wait_ack_list);
}

/**
 * \brief           Clear all packets from queue
 */
void xgl_reliable_clear(xgl_reliable_queue_t* queue) {
    if (queue == NULL) {
        return;
    }
    
    /* Remove and free all packets */
    xgl_list_node_t* node;
    while ((node = xgl_list_remove_head(&queue->wait_ack_list)) != NULL) {
        xgl_reliable_packet_t* packet = XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        free_reliable_packet(queue, packet);
    }
}

xgl_reliable_packet_t* xgl_reliable_find_packet_number(const xgl_reliable_queue_t* queue,
                                                       uint32_t packet_number,
                                                       uint16_t target_id) {
    if (queue == NULL) {
        return NULL;
    }
    
    /* Search for packet with matching packet number and target ID */
    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&queue->wait_ack_list, node) {
        xgl_reliable_packet_t* packet = XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
        
        if (packet->packet_number == packet_number && packet->target_id == target_id) {
            return packet;
        }
    }
    
    return NULL;
}

/**
 * \brief           Calculate exponential backoff timeout
 */
int32_t xgl_reliable_calc_backoff(int32_t initial_timeout_ms, uint8_t retry_count) {
    /* Exponential backoff: timeout = initial_timeout * 2^retry_count */
    int32_t backoff = initial_timeout_ms;
    
    /* Limit retry_count to prevent overflow */
    if (retry_count > 10) {
        retry_count = 10;
    }
    
    /* Calculate 2^retry_count */
    for (uint8_t i = 0; i < retry_count; i++) {
        backoff *= 2;
        
        /* Prevent overflow and cap at reasonable maximum */
        if (backoff > 30000) {  /* 30 seconds max */
            backoff = 30000;
            break;
        }
    }
    
    return backoff;
}
