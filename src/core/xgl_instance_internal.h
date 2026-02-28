/**
 * \file            xgl_instance_internal.h
 * \brief           Internal instance structure definition
 * \author          Nexus Team
 */

#ifndef XGL_INSTANCE_INTERNAL_H
#define XGL_INSTANCE_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xgl/xgl.h>
#include <xgl/xgl_tiered_pool.h>
#include <xgl/xgl_packet_pool.h>
#include <xgl/xgl_route.h>
#include <xgl/xgl_window.h>
#include <xgl/xgl_rtt.h>
#include <xgl/xgl_parser.h>

/*---------------------------------------------------------------------------*/
/* Internal Instance Structure                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Protocol instance internal structure
 */
struct xgl_instance {
    /*-----------------------------------------------------------------------*/
    /* Configuration                                                         */
    /*-----------------------------------------------------------------------*/
    xgl_config_t config;            /**< Instance configuration */
    bool initialized;               /**< Initialization flag */
    
    /*-----------------------------------------------------------------------*/
    /* Memory Management                                                     */
    /*-----------------------------------------------------------------------*/
    xgl_allocator_t* allocator;     /**< Memory allocator */
    xgl_tiered_pool_t tx_pool;      /**< Tiered TX memory pool */
    xgl_packet_pool_t packet_pool;  /**< Packet object pool */
    
    /*-----------------------------------------------------------------------*/
    /* Routing                                                               */
    /*-----------------------------------------------------------------------*/
    xgl_route_table_t route_table;  /**< Route table */
    
    /*-----------------------------------------------------------------------*/
    /* Sequence Numbers (per route)                                          */
    /*-----------------------------------------------------------------------*/
    uint8_t* seq_numbers;           /**< Sequence numbers array */
    size_t seq_numbers_count;       /**< Number of sequence numbers */
    
    /*-----------------------------------------------------------------------*/
    /* Transport Layer                                                       */
    /*-----------------------------------------------------------------------*/
    xgl_list_t wait_ack_list;       /**< Wait-ACK list */
    xgl_sliding_window_t* windows;  /**< Sliding windows (per route) */
    xgl_rtt_estimator_t* rtt_est;   /**< RTT estimators (per route) */
    size_t windows_count;           /**< Number of windows */
    
    /*-----------------------------------------------------------------------*/
    /* Data Link Layer                                                       */
    /*-----------------------------------------------------------------------*/
    xgl_list_t rx_parser_list;      /**< RX parser list */
    uint8_t* rx_buffer;             /**< RX buffer */
    size_t rx_buffer_size;          /**< RX buffer size */
    
    /*-----------------------------------------------------------------------*/
    /* Statistics                                                            */
    /*-----------------------------------------------------------------------*/
    xgl_statistics_t stats;         /**< Protocol statistics */
    
    /*-----------------------------------------------------------------------*/
    /* Thread Safety                                                         */
    /*-----------------------------------------------------------------------*/
#ifdef XGL_THREAD_SAFE
    xgl_mutex_t mutex;              /**< Instance mutex */
#endif
};

#ifdef __cplusplus
}
#endif

#endif /* XGL_INSTANCE_INTERNAL_H */

