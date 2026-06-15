/**
 * \file            xgl_packet_pool.c
 * \brief           Packet object pool implementation
 * \author          Nexus Team
 */

#include "xgl/internal/xgl_packet_pool.h"
#include "xgl/internal/xgl_list.h"
#include "xgl/xgl_error.h"
#include "xgl/internal/xgl_allocator.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using the configured allocator policy
 */
static void* alloc_mem(xgl_allocator_t* allocator, size_t size) {
    return xgl_alloc(allocator, size);
}

/**
 * \brief           Free memory using the configured allocator policy
 */
static void free_mem(xgl_allocator_t* allocator, void* ptr) {
    xgl_free(allocator, ptr);
}

/*---------------------------------------------------------------------------*/
/* Packet Pool Initialization                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize packet object pool
 */
int xgl_packet_pool_init(xgl_packet_pool_t* pool, size_t count,
                         xgl_allocator_t* allocator) {
    if (pool == NULL || count == 0) {
        return -1;
    }
    
    /* Initialize pool structure */
    memset(pool, 0, sizeof(xgl_packet_pool_t));
    pool->allocator = allocator;
    pool->total_count = count;
    pool->free_count = count;
    pool->peak_used = 0;
    
    /* Allocate packet array */
    pool->packets = (xgl_packet_t*)alloc_mem(allocator, 
                                             count * sizeof(xgl_packet_t));
    if (pool->packets == NULL) {
        return -1;
    }
    
    /* Initialize free list */
    xgl_list_init(&pool->free_list);
    
    /* Initialize all packets and add to free list */
    for (size_t i = 0; i < count; i++) {
        xgl_packet_t* packet = &pool->packets[i];
        memset(packet, 0, sizeof(xgl_packet_t));
        xgl_list_node_init(&packet->node);
        xgl_list_insert_tail(&pool->free_list, &packet->node);
    }
    
    return 0;
}

/**
 * \brief           Destroy packet object pool
 */
void xgl_packet_pool_destroy(xgl_packet_pool_t* pool) {
    if (pool == NULL) {
        return;
    }
    
    /* Free packet array */
    if (pool->packets != NULL) {
        free_mem(pool->allocator, pool->packets);
        pool->packets = NULL;
    }
    
    /* Reset pool structure */
    memset(pool, 0, sizeof(xgl_packet_pool_t));
}

/*---------------------------------------------------------------------------*/
/* Packet Allocation and Deallocation                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate a packet from the pool
 */
xgl_packet_t* xgl_packet_alloc(xgl_packet_pool_t* pool) {
    if (pool == NULL || xgl_list_is_empty(&pool->free_list)) {
        return NULL;
    }
    
    /* Remove packet from free list */
    xgl_list_node_t* node = xgl_list_remove_head(&pool->free_list);
    if (node == NULL) {
        return NULL;
    }
    
    /* Update statistics */
    pool->free_count--;
    size_t used_count = pool->total_count - pool->free_count;
    if (used_count > pool->peak_used) {
        pool->peak_used = used_count;
    }
    
    /* Get packet from node */
    xgl_packet_t* packet = XGL_LIST_ENTRY(node, xgl_packet_t, node);
    
    /* Initialize packet fields */
    memset(packet, 0, sizeof(xgl_packet_t));
    xgl_list_node_init(&packet->node);
    
    return packet;
}

/**
 * \brief           Free a packet back to the pool
 */
void xgl_packet_free(xgl_packet_pool_t* pool, xgl_packet_t* packet) {
    if (pool == NULL || packet == NULL) {
        return;
    }
    
    /* Decrement reference count on packet data if present */
    if (packet->data != NULL) {
        xgl_packet_data_unref(packet->data, pool->allocator);
        packet->data = NULL;
    }
    
    /* Clear packet fields */
    memset(packet, 0, sizeof(xgl_packet_t));
    xgl_list_node_init(&packet->node);
    
    /* Add packet back to free list */
    xgl_list_insert_tail(&pool->free_list, &packet->node);
    
    /* Update statistics */
    pool->free_count++;
}

/*---------------------------------------------------------------------------*/
/* Packet Data Reference Counting                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Increment packet data reference count
 */
void xgl_packet_data_ref(xgl_packet_data_t* data) {
    if (data == NULL) {
        return;
    }
    
    /* Increment reference count */
    data->ref_count++;
}

/**
 * \brief           Decrement packet data reference count and free if zero
 */
void xgl_packet_data_unref(xgl_packet_data_t* data, 
                           xgl_allocator_t* allocator) {
    if (data == NULL) {
        return;
    }
    
    /* Decrement reference count */
    if (data->ref_count > 0) {
        data->ref_count--;
    }
    
    /* Free data if reference count reaches zero */
    if (data->ref_count == 0) {
        if (data->owned_data != NULL) {
            free_mem(allocator, data->owned_data);
            data->owned_data = NULL;
            data->data = NULL;
        }
        free_mem(allocator, data);
    }
}

/**
 * \brief           Create packet data with reference count
 */
xgl_packet_data_t* xgl_packet_data_create(const uint8_t* data, size_t len,
                                          xgl_allocator_t* allocator) {
    if (data == NULL || len == 0) {
        return NULL;
    }
    
    /* Allocate packet data structure */
    xgl_packet_data_t* pkt_data = (xgl_packet_data_t*)alloc_mem(allocator,
                                                sizeof(xgl_packet_data_t));
    if (pkt_data == NULL) {
        return NULL;
    }
    
    /* Allocate data buffer */
    pkt_data->owned_data = (uint8_t*)alloc_mem(allocator, len);
    if (pkt_data->owned_data == NULL) {
        free_mem(allocator, pkt_data);
        return NULL;
    }
    
    /* Copy data and initialize fields */
    memcpy(pkt_data->owned_data, data, len);
    pkt_data->data = pkt_data->owned_data;
    pkt_data->data_len = len;
    pkt_data->ref_count = 1;  /* Initial reference count */
    
    return pkt_data;
}

/*---------------------------------------------------------------------------*/
/* Packet Pool Statistics                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get number of free packets in pool
 */
size_t xgl_packet_pool_get_free_count(const xgl_packet_pool_t* pool) {
    if (pool == NULL) {
        return 0;
    }
    return pool->free_count;
}

/**
 * \brief           Get number of used packets in pool
 */
size_t xgl_packet_pool_get_used_count(const xgl_packet_pool_t* pool) {
    if (pool == NULL) {
        return 0;
    }
    return pool->total_count - pool->free_count;
}

/**
 * \brief           Get peak number of packets used
 */
size_t xgl_packet_pool_get_peak_used(const xgl_packet_pool_t* pool) {
    if (pool == NULL) {
        return 0;
    }
    return pool->peak_used;
}

/**
 * \brief           Check if packet pool is empty (all packets free)
 */
bool xgl_packet_pool_is_empty(const xgl_packet_pool_t* pool) {
    if (pool == NULL) {
        return true;
    }
    return pool->free_count == pool->total_count;
}

/**
 * \brief           Check if packet pool is full (no free packets)
 */
bool xgl_packet_pool_is_full(const xgl_packet_pool_t* pool) {
    if (pool == NULL) {
        return true;
    }
    return pool->free_count == 0;
}

/**
 * \brief           Reset pool statistics
 */
void xgl_packet_pool_reset_stats(xgl_packet_pool_t* pool) {
    if (pool == NULL) {
        return;
    }
    pool->peak_used = pool->total_count - pool->free_count;
}
