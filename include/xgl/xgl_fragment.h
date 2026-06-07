/**
 * \file            xgl_fragment.h
 * \brief           Packet Fragmentation and Reassembly
 * \author          Nexus Team
 */

#ifndef XGL_FRAGMENT_H
#define XGL_FRAGMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "xgl_error.h"
#include "xgl_list.h"
#include "xgl_types.h"

/*---------------------------------------------------------------------------*/
/* Fragment Constants                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Maximum fragment ID value
 */
#define XGL_MAX_FRAGMENT_ID         255

/**
 * \brief           Fragment reassembly timeout in milliseconds
 */
#define XGL_FRAGMENT_TIMEOUT_MS     5000

/**
 * \brief           Serialized fragment control header size
 * \details         Wire layout: fragment_id, fragment_index,
 *                  total_fragments, fragment_offset_lsb, fragment_offset_msb.
 */
#define XGL_FRAGMENT_HEADER_SIZE    5

/*---------------------------------------------------------------------------*/
/* Fragment Structure                                                        */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Fragment information structure
 * \note            Embedded in packet data for fragmented packets
 */
typedef struct {
    uint8_t fragment_id;            /**< Fragment ID (unique per source) */
    uint8_t fragment_index;         /**< Fragment index (0-based) */
    uint8_t total_fragments;        /**< Total number of fragments */
    uint16_t fragment_offset;       /**< Offset in original data */
} xgl_fragment_info_t;

/*---------------------------------------------------------------------------*/
/* Reassembly Buffer Structure                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Fragment reassembly buffer
 * \note            Tracks fragments for a single fragmented message
 */
typedef struct {
    xgl_list_node_t node;           /**< List node for reassembly queue */
    
    /* Fragment identification */
    uint8_t fragment_id;            /**< Fragment ID */
    uint16_t source_id;             /**< Source node ID */
    uint8_t data_type;              /**< Data type */
    bool uses_fragment_ext;         /**< Uses production FRAGMENT_EXT key */
    uint32_t connection_id;         /**< Production connection ID */
    uint32_t session_epoch;         /**< Production session epoch */
    uint32_t message_id;            /**< Production message ID */
    
    /* Reassembly state */
    uint8_t total_fragments;        /**< Total number of fragments */
    uint8_t received_count;         /**< Number of fragments received */
    size_t received_bytes;          /**< Number of unique payload bytes received */
    uint8_t* received_bitmap;       /**< Bitmap of received fragments */
    
    /* Data buffer */
    uint8_t* data;                  /**< Reassembly data buffer */
    size_t data_len;                /**< Total data length */
    size_t buffer_size;             /**< Allocated buffer size */
    size_t expected_payload_size;   /**< Standard payload size for non-final fragments */
    size_t reserved_size;           /**< Bytes reserved against manager budget */
    
    /* Timeout tracking */
    uint32_t first_fragment_time;   /**< Timestamp of first fragment */
    uint32_t timeout_ms;            /**< Reassembly timeout */
    
} xgl_reassembly_buffer_t;

/*---------------------------------------------------------------------------*/
/* Fragmentation Manager Structure                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Fragmentation manager
 * \note            Manages fragment ID assignment and reassembly buffers
 */
typedef struct {
    /* Fragment ID tracking */
    uint8_t next_fragment_id;       /**< Next fragment ID to assign */
    
    /* Reassembly buffers */
    xgl_list_t reassembly_list;     /**< List of active reassembly buffers */
    size_t max_reassembly_buffers;  /**< Maximum concurrent reassembly buffers */
    
    /* Memory management */
    xgl_allocator_t* allocator;     /**< Memory allocator */
    
    /* Configuration */
    uint32_t reassembly_timeout_ms; /**< Reassembly timeout */
    size_t max_message_size;        /**< Maximum reassembled message bytes (0 = unlimited) */
    size_t max_reassembly_bytes;    /**< Maximum aggregate reserved bytes (0 = unlimited) */
    size_t current_reassembly_bytes;/**< Current aggregate reserved bytes */
    
} xgl_fragment_manager_t;

/*---------------------------------------------------------------------------*/
/* Fragmentation Functions                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize fragmentation manager
 * \param[in,out]   manager: Fragmentation manager structure
 * \param[in]       max_reassembly_buffers: Maximum concurrent reassembly buffers
 * \param[in]       reassembly_timeout_ms: Reassembly timeout in milliseconds
 * \param[in]       allocator: Memory allocator (NULL = malloc/free)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_fragment_init(xgl_fragment_manager_t* manager,
                              size_t max_reassembly_buffers,
                              uint32_t reassembly_timeout_ms,
                              xgl_allocator_t* allocator);

/**
 * \brief           Configure reassembly memory limits
 * \param[in,out]   manager: Fragmentation manager structure
 * \param[in]       max_message_size: Maximum complete message bytes (0 = unlimited)
 * \param[in]       max_reassembly_bytes: Aggregate in-flight bytes (0 = unlimited)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_fragment_set_limits(xgl_fragment_manager_t* manager,
                                    size_t max_message_size,
                                    size_t max_reassembly_bytes);

/**
 * \brief           Destroy fragmentation manager
 * \param[in,out]   manager: Fragmentation manager structure
 */
void xgl_fragment_destroy(xgl_fragment_manager_t* manager);

/**
 * \brief           Fragment data into multiple packets
 * \param[in]       manager: Fragmentation manager structure
 * \param[in]       data: Data buffer to fragment
 * \param[in]       data_len: Data length in bytes
 * \param[in]       max_fragment_size: Maximum fragment payload size
 * \param[out]      fragments: Array to store fragment data pointers
 * \param[out]      fragment_lens: Array to store fragment lengths
 * \param[in,out]   fragment_count: Input: array size, Output: number of fragments
 * \param[out]      fragment_id: Assigned fragment ID
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_fragment_data(xgl_fragment_manager_t* manager,
                              const uint8_t* data,
                              size_t data_len,
                              size_t max_fragment_size,
                              uint8_t** fragments,
                              size_t* fragment_lens,
                              size_t* fragment_count,
                              uint8_t* fragment_id);

/**
 * \brief           Process received fragment
 * \param[in,out]   manager: Fragmentation manager structure
 * \param[in]       source_id: Source node ID
 * \param[in]       data_type: Data type
 * \param[in]       fragment_data: Fragment data buffer
 * \param[in]       fragment_len: Fragment length in bytes
 * \param[out]      complete_data: Pointer to store complete data (if reassembly complete)
 * \param[out]      complete_len: Pointer to store complete data length
 * \param[in]       current_time_ms: Current time in milliseconds (0 = use system time)
 * \return          XGL_OK if reassembly complete, XGL_ERR_BUSY if waiting for more fragments, error code otherwise
 */
xgl_error_t xgl_fragment_process(xgl_fragment_manager_t* manager,
                                 uint16_t source_id,
                                 uint8_t data_type,
                                 const uint8_t* fragment_data,
                                 size_t fragment_len,
                                 uint8_t** complete_data,
                                 size_t* complete_len,
                                 uint32_t current_time_ms);

/**
 * \brief           Process received fragment described by FRAGMENT_EXT metadata
 * \param[in,out]   manager: Fragmentation manager structure
 * \param[in]       source_id: Source node ID
 * \param[in]       connection_id: Production connection ID
 * \param[in]       session_epoch: Production session epoch
 * \param[in]       data_type: Application data type
 * \param[in]       message_id: Fragmented message ID
 * \param[in]       fragment_offset: Payload offset in original message
 * \param[in]       message_len: Complete message length
 * \param[in]       fragment_payload: Fragment payload bytes
 * \param[in]       fragment_payload_len: Fragment payload length
 * \param[out]      complete_data: Pointer to complete data when ready
 * \param[out]      complete_len: Complete data length when ready
 * \param[in]       current_time_ms: Current time in milliseconds (0 = system time)
 * \return          XGL_OK if complete, XGL_ERR_BUSY if waiting, error otherwise
 */
xgl_error_t xgl_fragment_process_ext(xgl_fragment_manager_t* manager,
                                     uint16_t source_id,
                                     uint32_t connection_id,
                                     uint32_t session_epoch,
                                     uint8_t data_type,
                                     uint32_t message_id,
                                     uint32_t fragment_offset,
                                     uint32_t message_len,
                                     const uint8_t* fragment_payload,
                                     size_t fragment_payload_len,
                                     uint8_t** complete_data,
                                     size_t* complete_len,
                                     uint32_t current_time_ms);

/**
 * \brief           Process reassembly timeouts
 * \param[in,out]   manager: Fragmentation manager structure
 * \param[in]       current_time_ms: Current time in milliseconds
 * \return          Number of reassembly buffers timed out and freed
 */
uint32_t xgl_fragment_process_timeouts(xgl_fragment_manager_t* manager,
                                       uint32_t current_time_ms);

/**
 * \brief           Get number of active reassembly buffers
 * \param[in]       manager: Fragmentation manager structure
 * \return          Number of active reassembly buffers
 */
size_t xgl_fragment_get_reassembly_count(const xgl_fragment_manager_t* manager);

/**
 * \brief           Clear all reassembly buffers
 * \param[in,out]   manager: Fragmentation manager structure
 */
void xgl_fragment_clear_reassembly(xgl_fragment_manager_t* manager);

/**
 * \brief           Free fragment data allocated by xgl_fragment_data
 * \param[in]       manager: Fragmentation manager structure
 * \param[in]       fragments: Array of fragment data pointers
 * \param[in]       fragment_count: Number of fragments
 */
void xgl_fragment_free_fragments(xgl_fragment_manager_t* manager,
                                 uint8_t** fragments,
                                 size_t fragment_count);

/**
 * \brief           Free complete data allocated by xgl_fragment_process
 * \param[in]       manager: Fragmentation manager structure
 * \param[in]       data: Data pointer to free
 */
void xgl_fragment_free_data(xgl_fragment_manager_t* manager,
                            uint8_t* data);

#ifdef __cplusplus
}
#endif

#endif /* XGL_FRAGMENT_H */
