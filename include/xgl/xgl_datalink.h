/**
 * \file            xgl_datalink.h
 * \brief           Data link layer interface
 * \author          Nexus Team
 */

#ifndef XGL_DATALINK_H
#define XGL_DATALINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_types.h"
#include "xgl_error.h"
#include "xgl_frame.h"
#include "xgl_parser.h"
#include "xgl_layer_interface.h"

/*---------------------------------------------------------------------------*/
/* Forward Declarations                                                      */
/*---------------------------------------------------------------------------*/

/* Forward declare network context */
struct xgl_network_ctx_s;

/*---------------------------------------------------------------------------*/
/* Data Link Layer Context                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Data link layer context structure
 */
typedef struct xgl_datalink_ctx_s {
    xgl_parser_t parser;            /**< Frame parser */
    uint8_t* rx_cache;              /**< RX cache buffer */
    size_t rx_cache_size;           /**< RX cache size */
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* rx_crc8_errors;       /**< CRC8 error counter pointer */
    uint64_t* rx_crc16_errors;      /**< CRC16 error counter pointer */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;       /**< User data for callbacks */
    xgl_handle_t owner_handle;      /**< Owning protocol instance handle */
    xgl_allocator_t* allocator;     /**< Allocator for large temporary TX buffers */
    uint16_t source_id;             /**< Local source ID */
    bool auth_required;             /**< Require authenticated production frames */
    uint32_t auth_key_id;           /**< Active authentication key id */
    xgl_auth_provider_t* auth_provider; /**< Authentication callback provider */
    
    /* Layer interface for decoupled communication */
    xgl_layer_interface_t* upper_layer; /**< Upper layer interface (network) */
} xgl_datalink_ctx_t;

/**
 * \brief           Data link layer configuration structure
 */
typedef struct {
    uint8_t* rx_cache;              /**< RX cache buffer */
    size_t rx_cache_size;           /**< RX cache size */
    uint16_t source_id;             /**< Local source ID */
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* rx_crc8_errors;       /**< CRC8 error counter pointer (can be NULL) */
    uint64_t* rx_crc16_errors;      /**< CRC16 error counter pointer (can be NULL) */
    xgl_layer_interface_t* upper_layer; /**< Upper layer interface (can be NULL) */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data;       /**< User data for callbacks (can be NULL) */
    xgl_handle_t owner_handle;      /**< Owning protocol instance handle (can be NULL) */
    xgl_allocator_t* allocator;     /**< Allocator for large temporary TX buffers (NULL = default) */
    bool auth_required;             /**< Require authenticated production frames */
    uint32_t auth_key_id;           /**< Active authentication key id */
    xgl_auth_provider_t* auth_provider; /**< Authentication callback provider */
} xgl_datalink_config_t;

/*---------------------------------------------------------------------------*/
/* Data Link Layer Functions                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize data link layer context with configuration structure
 * \param[out]      ctx: Data link layer context
 * \param[in]       config: Configuration structure
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_init(xgl_datalink_ctx_t* ctx,
                              const xgl_datalink_config_t* config);

/**
 * \brief           Send frame via physical layer
 * \param[in]       ctx: Data link layer context
 * \param[in]       phy: Physical layer operations
 * \param[in]       frame: Frame structure to send
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_send(xgl_datalink_ctx_t* ctx,
                              xgl_phy_ops_t* phy,
                              const xgl_frame_t* frame);

/**
 * \brief           Send raw frame buffer via physical layer
 * \param[in]       ctx: Data link layer context
 * \param[in]       phy: Physical layer operations
 * \param[in]       frame_buffer: Frame buffer to send
 * \param[in]       frame_len: Frame length
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_send_raw(xgl_datalink_ctx_t* ctx,
                                  xgl_phy_ops_t* phy,
                                  const uint8_t* frame_buffer,
                                  size_t frame_len);

/**
 * \brief           Receive and parse frames from physical layer
 * \param[in,out]   ctx: Data link layer context
 * \param[in]       phy: Physical layer operations
 * \param[in]       current_time_ms: Current time in milliseconds
 * \param[in]       timeout_ms: Parser timeout in milliseconds
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_receive(xgl_datalink_ctx_t* ctx,
                                 xgl_phy_ops_t* phy,
                                 uint32_t current_time_ms,
                                 uint32_t timeout_ms);

/**
 * \brief           Process received frame
 * \param[in]       ctx: Data link layer context
 * \param[in]       frame_buffer: Complete frame buffer
 * \param[in]       frame_len: Frame length
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_process_frame(xgl_datalink_ctx_t* ctx,
                                       const uint8_t* frame_buffer,
                                       size_t frame_len);

/**
 * \brief           Get datalink layer interface
 * \details         Returns the layer interface for this datalink instance
 * \param[in]       ctx: Datalink layer context
 * \param[out]      iface: Layer interface structure to initialize
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_get_interface(xgl_datalink_ctx_t* ctx,
                                      xgl_layer_interface_t* iface);

#ifdef __cplusplus
}
#endif

#endif /* XGL_DATALINK_H */
