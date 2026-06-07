/**
 * \file            xgl_packet_pool.h
 * \brief           Packet object pool for efficient packet allocation
 * \author          Nexus Team
 */

#ifndef XGL_PACKET_POOL_H
#define XGL_PACKET_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_list.h"
#include "xgl_types.h"

/*---------------------------------------------------------------------------*/
/* Packet Structure                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Protocol packet structure
 */
typedef struct xgl_packet {
    /*-----------------------------------------------------------------------*/
    /* Addressing                                                            */
    /*-----------------------------------------------------------------------*/
    uint16_t source_id;             /**< Source node ID */
    uint16_t target_id;             /**< Target node ID */
    uint16_t session_id;            /**< Transport session/epoch ID */
    uint32_t connection_id;         /**< Production connection context ID */
    uint32_t packet_number;         /**< Monotonic production packet number */
    uint32_t session_epoch;         /**< Production session epoch */
    
    /*-----------------------------------------------------------------------*/
    /* Attributes                                                            */
    /*-----------------------------------------------------------------------*/
    uint8_t version;                /**< Protocol version */
    uint8_t packet_type;            /**< Production packet type */
    uint8_t flags;                  /**< Production wire flags */
    uint8_t data_type;              /**< Data type */
    uint8_t reliable;               /**< Reliable transmission flag */
    uint8_t fragment;               /**< Fragment flag */
    uint8_t encrypt;                /**< Encryption type */
    uint8_t priority;               /**< Priority level (0-7) */
    uint8_t ttl;                    /**< Hop limit */
    uint8_t traffic_class;          /**< Priority and traffic class */
    uint8_t compress;               /**< Compression type */
    
    /*-----------------------------------------------------------------------*/
    /* Data                                                                  */
    /*-----------------------------------------------------------------------*/
    xgl_packet_data_t* data;        /**< Pointer to packet data */
    const uint8_t* extensions;      /**< Production TLV extension bytes */
    size_t extensions_len;          /**< Length of production TLV extensions */
    
    /*-----------------------------------------------------------------------*/
    /* Retransmission                                                        */
    /*-----------------------------------------------------------------------*/
    uint8_t retry_count;            /**< Current retry count */
    int32_t wait_time_ms;           /**< Wait time in milliseconds */
    uint32_t send_timestamp;        /**< Send timestamp */
    
    /*-----------------------------------------------------------------------*/
    /* Routing                                                               */
    /*-----------------------------------------------------------------------*/
    xgl_phy_ops_t* phy;             /**< Physical layer operations */
    
    /*-----------------------------------------------------------------------*/
    /* List Node                                                             */
    /*-----------------------------------------------------------------------*/
    xgl_list_node_t node;           /**< List node for linking */
    
} xgl_packet_t;

/*---------------------------------------------------------------------------*/
/* Packet Pool Structure                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Packet object pool structure
 */
typedef struct {
    xgl_packet_t* packets;          /**< Array of pre-allocated packets */
    xgl_list_t free_list;           /**< Free list of available packets */
    size_t total_count;             /**< Total number of packets */
    size_t free_count;              /**< Number of free packets */
    size_t peak_used;               /**< Peak number of packets used */
    xgl_allocator_t* allocator;     /**< Memory allocator */
} xgl_packet_pool_t;

/*---------------------------------------------------------------------------*/
/* Packet Pool Initialization                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize packet object pool
 * \param[in,out]   pool: Pointer to packet pool structure
 * \param[in]       count: Number of packets to pre-allocate
 * \param[in]       allocator: Memory allocator; NULL fallback is build-policy controlled
 * \return          0 on success, -1 on error
 */
int xgl_packet_pool_init(xgl_packet_pool_t* pool, size_t count,
                         xgl_allocator_t* allocator);

/**
 * \brief           Destroy packet object pool
 * \param[in,out]   pool: Pointer to packet pool structure
 */
void xgl_packet_pool_destroy(xgl_packet_pool_t* pool);

/*---------------------------------------------------------------------------*/
/* Packet Allocation and Deallocation                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate a packet from the pool
 * \param[in,out]   pool: Pointer to packet pool structure
 * \return          Pointer to allocated packet, NULL if pool is exhausted
 */
xgl_packet_t* xgl_packet_alloc(xgl_packet_pool_t* pool);

/**
 * \brief           Free a packet back to the pool
 * \param[in,out]   pool: Pointer to packet pool structure
 * \param[in]       packet: Pointer to packet to free
 */
void xgl_packet_free(xgl_packet_pool_t* pool, xgl_packet_t* packet);

/*---------------------------------------------------------------------------*/
/* Packet Data Reference Counting                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Increment packet data reference count
 * \param[in,out]   data: Pointer to packet data
 */
void xgl_packet_data_ref(xgl_packet_data_t* data);

/**
 * \brief           Decrement packet data reference count and free if zero
 * \param[in,out]   data: Pointer to packet data
 * \param[in]       allocator: Memory allocator; NULL fallback is build-policy controlled
 */
void xgl_packet_data_unref(xgl_packet_data_t* data, 
                           xgl_allocator_t* allocator);

/**
 * \brief           Create packet data with reference count
 * \param[in]       data: Pointer to data buffer
 * \param[in]       len: Data length in bytes
 * \param[in]       allocator: Memory allocator; NULL fallback is build-policy controlled
 * \return          Pointer to packet data, NULL on allocation failure
 */
xgl_packet_data_t* xgl_packet_data_create(const uint8_t* data, size_t len,
                                          xgl_allocator_t* allocator);

/*---------------------------------------------------------------------------*/
/* Packet Pool Statistics                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get number of free packets in pool
 * \param[in]       pool: Pointer to packet pool structure
 * \return          Number of free packets
 */
size_t xgl_packet_pool_get_free_count(const xgl_packet_pool_t* pool);

/**
 * \brief           Get number of used packets in pool
 * \param[in]       pool: Pointer to packet pool structure
 * \return          Number of used packets
 */
size_t xgl_packet_pool_get_used_count(const xgl_packet_pool_t* pool);

/**
 * \brief           Get peak number of packets used
 * \param[in]       pool: Pointer to packet pool structure
 * \return          Peak number of packets used
 */
size_t xgl_packet_pool_get_peak_used(const xgl_packet_pool_t* pool);

/**
 * \brief           Check if packet pool is empty (all packets free)
 * \param[in]       pool: Pointer to packet pool structure
 * \return          true if all packets are free, false otherwise
 */
bool xgl_packet_pool_is_empty(const xgl_packet_pool_t* pool);

/**
 * \brief           Check if packet pool is full (no free packets)
 * \param[in]       pool: Pointer to packet pool structure
 * \return          true if no free packets, false otherwise
 */
bool xgl_packet_pool_is_full(const xgl_packet_pool_t* pool);

/**
 * \brief           Reset pool statistics
 * \param[in,out]   pool: Pointer to packet pool structure
 */
void xgl_packet_pool_reset_stats(xgl_packet_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* XGL_PACKET_POOL_H */
