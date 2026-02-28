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

/*---------------------------------------------------------------------------*/
/* Data Link Layer Context                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Data link layer context structure
 */
typedef struct {
    xgl_parser_t parser;            /**< Frame parser */
    uint8_t* rx_cache;              /**< RX cache buffer */
    size_t rx_cache_size;           /**< RX cache size */
    xgl_statistics_t* stats;        /**< Statistics pointer */
    xgl_rx_callback_t rx_callback;  /**< Receive callback */
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;       /**< User data for callbacks */
    uint8_t source_id;              /**< Local source ID */
} xgl_datalink_ctx_t;

/*---------------------------------------------------------------------------*/
/* Data Link Layer Functions                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize data link layer context
 * \param[out]      ctx: Data link layer context
 * \param[in]       rx_cache: RX cache buffer
 * \param[in]       rx_cache_size: RX cache size
 * \param[in]       stats: Statistics structure pointer
 * \param[in]       source_id: Local source ID
 * \param[in]       rx_callback: Receive callback
 * \param[in]       error_callback: Error callback
 * \param[in]       callback_user_data: User data for callbacks
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_init(xgl_datalink_ctx_t* ctx,
                              uint8_t* rx_cache,
                              size_t rx_cache_size,
                              xgl_statistics_t* stats,
                              uint8_t source_id,
                              xgl_rx_callback_t rx_callback,
                              xgl_error_callback_t error_callback,
                              void* callback_user_data);

/**
 * \brief           Send frame via physical layer
 * \param[in]       phy: Physical layer operations
 * \param[in]       frame: Frame structure to send
 * \param[in]       stats: Statistics structure pointer (can be NULL)
 * \param[in]       error_callback: Error callback (can be NULL)
 * \param[in]       callback_user_data: User data for error callback
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_send(xgl_phy_ops_t* phy,
                              const xgl_frame_t* frame,
                              xgl_statistics_t* stats,
                              xgl_error_callback_t error_callback,
                              void* callback_user_data);

/**
 * \brief           Send raw frame buffer via physical layer
 * \param[in]       phy: Physical layer operations
 * \param[in]       frame_buffer: Frame buffer to send
 * \param[in]       frame_len: Frame length
 * \param[in]       stats: Statistics structure pointer (can be NULL)
 * \param[in]       error_callback: Error callback (can be NULL)
 * \param[in]       callback_user_data: User data for error callback
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_datalink_send_raw(xgl_phy_ops_t* phy,
                                  const uint8_t* frame_buffer,
                                  size_t frame_len,
                                  xgl_statistics_t* stats,
                                  xgl_error_callback_t error_callback,
                                  void* callback_user_data);

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

#ifdef __cplusplus
}
#endif

#endif /* XGL_DATALINK_H */
