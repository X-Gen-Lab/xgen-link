/**
 * \file            xgl_datalink.h
 * \brief           Data link layer interface
 * \author          X-Gen Lab
 */

#ifndef XGL_DATALINK_H
#define XGL_DATALINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xgl/internal/xgl_frame.h"
#include "xgl/internal/xgl_parser.h"
#include "xgl/internal/xgl_protocol_io.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"
#if XGL_FEATURE_AUTH
#include "xgl/internal/xgl_security.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

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
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* rx_header_crc_errors; /**< Header CRC error counter pointer */
    uint64_t* rx_crc16_errors;      /**< Frame CRC16 error counter pointer */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;            /**< User data for callbacks */
    xgl_handle_t owner_handle;           /**< Owning protocol instance handle */
    const xgm_allocator_t* allocator; /**< Allocator for temporary TX buffers */
    uint16_t source_id;               /**< Local source ID */
#if XGL_FEATURE_AUTH
    xgl_security_ctx_t* security; /**< Borrowed instance security state */
#endif

    /* Layer interface for decoupled communication */
    xgl_frame_interface_t* upper_layer; /**< Upper layer interface (network) */
} xgl_datalink_ctx_t;

/**
 * \brief           Data link layer configuration structure
 */
typedef struct {
    uint16_t source_id;             /**< Local source ID */
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* rx_header_crc_errors; /**< Header CRC error counter pointer (can
                                       be NULL) */
    uint64_t*
        rx_crc16_errors; /**< Frame CRC16 error counter pointer (can be NULL) */
    xgl_frame_interface_t*
        upper_layer; /**< Upper layer interface (can be NULL) */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data; /**< User data for callbacks (can be NULL) */
    xgl_handle_t
        owner_handle; /**< Owning protocol instance handle (can be NULL) */
    const xgm_allocator_t*
        allocator; /**< Allocator for temporary TX buffers; NULL
                              fallback is build-policy controlled */
#if XGL_FEATURE_AUTH
    xgl_security_ctx_t* security; /**< Borrowed instance security state */
#endif
} xgl_datalink_config_t;

/*---------------------------------------------------------------------------*/
/* Data Link Layer Functions                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize data link layer context with configuration
 * structure
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
xgl_error_t xgl_datalink_send(xgl_datalink_ctx_t* ctx, xgl_phy_ops_t* phy,
                              const xgl_frame_t* frame);

/**
 * \brief           Encode in caller storage and submit without moving payload
 * \param[in,out]   ctx: Shared datalink services
 * \param[in]       phy: Synchronous transmit operations
 * \param[in]       frame: Borrowed logical frame
 * \param[in,out]   buffer: Caller frame storage
 * \param[in]       buffer_size: Available storage bytes
 * \param[in]       payload_offset: Payload offset fixed by the caller
 * \return          XGL_OK or a layout, security or driver error
 */
xgl_error_t xgl_datalink_send_inplace(xgl_datalink_ctx_t* ctx,
                                      xgl_phy_ops_t* phy,
                                      const xgl_frame_t* frame, uint8_t* buffer,
                                      size_t buffer_size,
                                      size_t payload_offset);

/**
 * \brief           Send raw frame buffer via physical layer
 * \param[in]       ctx: Data link layer context
 * \param[in]       phy: Physical layer operations
 * \param[in]       frame_buffer: Frame buffer to send
 * \param[in]       frame_len: Frame length
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_send_raw(xgl_datalink_ctx_t* ctx, xgl_phy_ops_t* phy,
                                  const uint8_t* frame_buffer,
                                  size_t frame_len);

/**
 * \brief           Poll one PHY using its own parser and shared link policy
 * \param[in,out]   ctx: Shared security, statistics and delivery context
 * \param[in,out]   parser: Parser owned by the selected PHY
 * \param[in]       phy: Physical layer operations
 * \param[in]       current_time_ms: Current time in milliseconds
 * \param[in]       timeout_ms: Parser timeout in milliseconds
 * \param[in]       byte_budget: Maximum bytes to read in this call
 * \return          XGL_OK on success, error code otherwise
 * \note            A single read is capped at XGL_DATALINK_RX_CHUNK_SIZE.
 */
xgl_error_t xgl_datalink_poll_parser(xgl_datalink_ctx_t* ctx,
                                     xgl_parser_t* parser, xgl_phy_ops_t* phy,
                                     uint32_t current_time_ms,
                                     uint32_t timeout_ms, size_t byte_budget);

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
                                       xgl_frame_interface_t* iface);

#ifdef __cplusplus
}
#endif

#endif /* XGL_DATALINK_H */
