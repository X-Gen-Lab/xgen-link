/**
 * \file            xgl_reliable.h
 * \brief           Reliable Transmission Queue Management
 * \author          Nexus Team
 */

#ifndef XGL_RELIABLE_H
#define XGL_RELIABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "xgl_error.h"
#include "xgl_list.h"
#include "xgl_types.h"
#include "xgl_wire.h"

/*---------------------------------------------------------------------------*/
/* Reliable Packet Structure                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Reliable packet structure for wait-ACK queue
 * \note            Contains packet data and retransmission state
 */
typedef struct {
    xgl_list_node_t node;           /**< List node for queue */
    
    /* Packet data */
    uint8_t* data;                  /**< Packet data buffer */
    size_t data_len;                /**< Data length in bytes */
    
    /* Addressing */
    uint16_t source_id;             /**< Source node ID */
    uint16_t target_id;             /**< Target node ID */
    uint32_t packet_number;         /**< 32-bit production packet number */
    uint8_t seq_num;                /**< Sequence number */
    uint16_t session_id;            /**< Transport session/epoch ID */
    uint8_t data_type;              /**< Data type */
    
    /* Attributes */
    uint8_t priority;               /**< Priority level (0-7) */
    
    /* Retransmission state */
    uint8_t retry_count;            /**< Current retry count */
    uint32_t send_timestamp;        /**< Last send timestamp in ms */
    int32_t timeout_ms;             /**< Current timeout value in ms */
    int32_t initial_timeout_ms;     /**< Initial timeout value in ms */
    
    /* Routing */
    xgl_phy_ops_t* phy;             /**< Physical layer for this packet */
    
} xgl_reliable_packet_t;

/*---------------------------------------------------------------------------*/
/* Reliable Queue Structure                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Reliable transmission queue
 * \note            Manages wait-ACK queue with timeout tracking
 */
typedef struct {
    xgl_list_t wait_ack_list;       /**< List of packets waiting for ACK */
    uint8_t max_retry_count;        /**< Maximum retry count */
    xgl_allocator_t* allocator;     /**< Memory allocator */
} xgl_reliable_queue_t;

/*---------------------------------------------------------------------------*/
/* Reliable Queue Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize reliable transmission queue
 * \param[in,out]   queue: Reliable queue structure
 * \param[in]       max_retry_count: Maximum retry count
 * \param[in]       allocator: Memory allocator (NULL = malloc/free)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_reliable_init(xgl_reliable_queue_t* queue,
                              uint8_t max_retry_count,
                              xgl_allocator_t* allocator);

/**
 * \brief           Destroy reliable transmission queue
 * \param[in,out]   queue: Reliable queue structure
 */
void xgl_reliable_destroy(xgl_reliable_queue_t* queue);

/**
 * \brief           Add packet to wait-ACK queue
 * \param[in,out]   queue: Reliable queue structure
 * \param[in]       data: Packet data buffer
 * \param[in]       data_len: Data length in bytes
 * \param[in]       source_id: Source node ID
 * \param[in]       target_id: Target node ID
 * \param[in]       seq_num: Sequence number
 * \param[in]       data_type: Data type
 * \param[in]       priority: Priority level (0-7)
 * \param[in]       timeout_ms: Initial timeout in milliseconds
 * \param[in]       phy: Physical layer operations
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_reliable_add_packet(xgl_reliable_queue_t* queue,
                                    const uint8_t* data,
                                    size_t data_len,
                                    uint16_t source_id,
                                    uint16_t target_id,
                                    uint8_t seq_num,
                                    uint8_t data_type,
                                    uint8_t priority,
                                    int32_t timeout_ms,
                                    xgl_phy_ops_t* phy);

xgl_error_t xgl_reliable_add_packet_number(xgl_reliable_queue_t* queue,
                                           const uint8_t* data,
                                           size_t data_len,
                                           uint16_t source_id,
                                           uint16_t target_id,
                                           uint32_t packet_number,
                                           uint8_t data_type,
                                           uint8_t priority,
                                           int32_t timeout_ms,
                                           xgl_phy_ops_t* phy);

/**
 * \brief           Remove packet from wait-ACK queue by sequence number
 * \param[in,out]   queue: Reliable queue structure
 * \param[in]       seq_num: Sequence number to remove
 * \param[in]       target_id: Target node ID
 * \return          XGL_OK on success, XGL_ERR_NOT_FOUND if not found
 */
xgl_error_t xgl_reliable_remove_packet(xgl_reliable_queue_t* queue,
                                       uint8_t seq_num,
                                       uint16_t target_id);

xgl_error_t xgl_reliable_remove_packet_number(xgl_reliable_queue_t* queue,
                                              uint32_t packet_number,
                                              uint16_t target_id);

/**
 * \brief           Remove acknowledged packets described by ACK ranges
 * \details         Each range length is a packet count. The first range starts
 *                  at largest_ack; later ranges skip gap unacknowledged packets.
 * \param[in,out]   queue: Reliable queue structure
 * \param[in]       target_id: Target node ID
 * \param[in]       largest_ack: Largest acknowledged packet number
 * \param[in]       ranges: ACK ranges
 * \param[in]       range_count: Number of ACK ranges
 * \return          Number of packets removed from the wait-ACK queue
 */
size_t xgl_reliable_remove_ack_ranges(xgl_reliable_queue_t* queue,
                                      uint16_t target_id,
                                      uint32_t largest_ack,
                                      const xgl_wire_ack_range_t* ranges,
                                      size_t range_count);

/**
 * \brief           Process timeouts and retransmit packets
 * \param[in,out]   queue: Reliable queue structure
 * \param[in]       current_time_ms: Current time in milliseconds
 * \param[out]      retry_exhausted: Pointer to store retry exhausted packet (optional)
 * \return          Number of packets retransmitted
 */
uint32_t xgl_reliable_process_timeouts(xgl_reliable_queue_t* queue,
                                       uint32_t current_time_ms,
                                       xgl_reliable_packet_t** retry_exhausted);

/**
 * \brief           Get number of packets in wait-ACK queue
 * \param[in]       queue: Reliable queue structure
 * \return          Number of packets waiting for ACK
 */
size_t xgl_reliable_get_count(const xgl_reliable_queue_t* queue);

/**
 * \brief           Check if queue is empty
 * \param[in]       queue: Reliable queue structure
 * \return          true if queue is empty, false otherwise
 */
bool xgl_reliable_is_empty(const xgl_reliable_queue_t* queue);

/**
 * \brief           Clear all packets from queue
 * \param[in,out]   queue: Reliable queue structure
 */
void xgl_reliable_clear(xgl_reliable_queue_t* queue);

/**
 * \brief           Find packet by sequence number
 * \param[in]       queue: Reliable queue structure
 * \param[in]       seq_num: Sequence number to find
 * \param[in]       target_id: Target node ID
 * \return          Pointer to packet, NULL if not found
 */
xgl_reliable_packet_t* xgl_reliable_find_packet(const xgl_reliable_queue_t* queue,
                                                uint8_t seq_num,
                                                uint16_t target_id);

xgl_reliable_packet_t* xgl_reliable_find_packet_number(const xgl_reliable_queue_t* queue,
                                                       uint32_t packet_number,
                                                       uint16_t target_id);

/**
 * \brief           Calculate exponential backoff timeout
 * \param[in]       initial_timeout_ms: Initial timeout in milliseconds
 * \param[in]       retry_count: Current retry count
 * \return          Backoff timeout in milliseconds
 */
int32_t xgl_reliable_calc_backoff(int32_t initial_timeout_ms, uint8_t retry_count);

#ifdef __cplusplus
}
#endif

#endif /* XGL_RELIABLE_H */
