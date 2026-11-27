/**
 * \file            xgl_reliable.h
 * \brief           Reliable Transmission Queue Management
 * \author          X-Gen Lab
 */

#ifndef XGL_RELIABLE_H
#define XGL_RELIABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "internal/xgl_list.h"
#include "wire/xgl_wire.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"

/*---------------------------------------------------------------------------*/
/* Reliable Packet Structure                                                */
/*---------------------------------------------------------------------------*/

#include "xgl/xgl_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XGL_RELIABLE_INDEX_BUCKETS XGL_RELIABLE_BUCKET_COUNT

/**
 * \brief           Reliable packet structure for wait-ACK queue
 * \note            Contains packet data and retransmission state
 */
typedef struct xgl_reliable_packet_s {
    xgl_list_node_t node;                     /**< List node for queue */
    struct xgl_reliable_packet_s* index_next; /**< Hash bucket link */

    /* Packet data */
    uint8_t* data;   /**< Packet data buffer */
    size_t data_len; /**< Data length in bytes */

    /* Addressing */
    uint16_t source_id;     /**< Source node ID */
    uint16_t target_id;     /**< Target node ID */
    uint32_t packet_number; /**< 32-bit production packet number */
    uint32_t connection_id; /**< Production connection context ID */
    uint32_t session_epoch; /**< Production session epoch */
    uint8_t data_type;      /**< Data type */

    /* Attributes */
    uint8_t packet_type; /**< Production packet type */
    uint8_t priority;    /**< Priority level (0-7) */

    /* Retransmission state */
    uint8_t retry_count; /**< Current retry count */
    bool sent; /**< First transmission accepted; timestamp zero is valid */
    bool retry_pending;         /**< A local-capacity retry is scheduled */
    bool fast_retry_pending;    /**< Validated SACK requests deferred work */
    uint32_t retry_started_ms;  /**< Start of the bounded local retry delay */
    uint32_t send_timestamp;    /**< Last send timestamp in ms */
    int32_t timeout_ms;         /**< Current timeout value in ms */
    int32_t initial_timeout_ms; /**< Initial timeout value in ms */

} xgl_reliable_packet_t;

/*---------------------------------------------------------------------------*/
/* Reliable Queue Structure                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Reliable transmission queue
 * \note            Manages wait-ACK queue with timeout tracking
 */
typedef struct {
    xgl_list_t wait_ack_list; /**< List of packets waiting for ACK */
    xgl_reliable_packet_t*
        index_buckets[XGL_RELIABLE_INDEX_BUCKETS]; /**< Packet lookup buckets */
    const xgl_allocator_t* data_allocator;         /**< Owned payload storage */
    const xgl_allocator_t* allocator;              /**< Memory allocator */
} xgl_reliable_queue_t;

/*---------------------------------------------------------------------------*/
/* Reliable Queue Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize reliable transmission queue
 * \param[in,out]   queue: Reliable queue structure
 * \param[in]       allocator: Required allocator service
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_reliable_init(xgl_reliable_queue_t* queue,
                              const xgl_allocator_t* allocator);

/**
 * \brief           Destroy reliable transmission queue
 * \param[in,out]   queue: Reliable queue structure
 */
void xgl_reliable_destroy(xgl_reliable_queue_t* queue);

/**
 * \brief           Copy a payload into one indexed reliable record
 * \param[out]      packet_out: Optional admitted record; NULL on failure
 * \note            The returned record stays owned by queue until removal.
 */
xgl_error_t xgl_reliable_add_packet_number(
    xgl_reliable_queue_t* queue, const uint8_t* data, size_t data_len,
    uint16_t source_id, uint16_t target_id, uint32_t packet_number,
    uint8_t data_type, uint8_t priority, int32_t timeout_ms,
    xgl_reliable_packet_t** packet_out);

xgl_error_t xgl_reliable_remove_packet_number(xgl_reliable_queue_t* queue,
                                              uint32_t packet_number,
                                              uint16_t target_id);

/**
 * \brief           Remove a known live record without a second number lookup
 * \param[in,out]   queue: Queue expected to own the record
 * \param[in]       packet: Live record; invalid after successful removal
 * \return          XGL_OK, XGL_ERR_NULL_POINTER or XGL_ERR_SEQUENCE_ERROR
 * \note            A foreign live record is rejected without changing either
 *                  queue. A dangling pointer is never a valid input.
 */
xgl_error_t xgl_reliable_remove_owned(xgl_reliable_queue_t* queue,
                                      xgl_reliable_packet_t* packet);

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

xgl_reliable_packet_t*
xgl_reliable_find_packet_number(const xgl_reliable_queue_t* queue,
                                uint32_t packet_number, uint16_t target_id);

/**
 * \brief           Calculate exponential backoff timeout
 * \param[in]       initial_timeout_ms: Initial timeout in milliseconds
 * \param[in]       retry_count: Current retry count
 * \return          Backoff timeout in milliseconds
 */
int32_t xgl_reliable_calc_backoff(int32_t initial_timeout_ms,
                                  uint8_t retry_count);

#ifdef __cplusplus
}
#endif

#endif /* XGL_RELIABLE_H */
