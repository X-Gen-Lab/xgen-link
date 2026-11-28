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

#include "datalink/xgl_parser.h"
#include "internal/xgl_allocator.h"
#include "internal/xgl_protocol_io.h"
#include "wire/xgl_frame.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_diagnostics.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"
#if XGL_FEATURE_AUTH
#include "security/xgl_security.h"
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
#if XGL_FEATURE_STATISTICS
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* rx_header_crc_errors; /**< Header CRC error counter pointer */
    uint64_t* rx_crc16_errors;      /**< Frame CRC16 error counter pointer */
#endif
    xgl_error_callback_t error_callback; /**< Error callback */
    void* callback_user_data;            /**< User data for callbacks */
    xgl_handle_t owner_handle;           /**< Owning protocol instance handle */
    const xgl_allocator_t* allocator; /**< Allocator for temporary TX buffers */
#if XGL_FEATURE_AUTH
    xgl_security_ctx_t* security; /**< Borrowed instance security state */
#endif

    /* Layer interface for decoupled communication */
    xgl_frame_rx_endpoint_t*
        upper_layer; /**< Upper layer interface (network) */
} xgl_datalink_ctx_t;

/**
 * \brief           Data link layer configuration structure
 */
typedef struct {
    xgl_layer_stats_t* stats;       /**< Layer statistics pointer */
    uint64_t* rx_header_crc_errors; /**< Header CRC error counter pointer (can
                                       be NULL) */
    uint64_t*
        rx_crc16_errors; /**< Frame CRC16 error counter pointer (can be NULL) */
    xgl_frame_rx_endpoint_t*
        upper_layer; /**< Upper layer interface (can be NULL) */
    xgl_error_callback_t error_callback; /**< Error callback (can be NULL) */
    void* callback_user_data; /**< User data for callbacks (can be NULL) */
    xgl_handle_t
        owner_handle; /**< Owning protocol instance handle (can be NULL) */
    const xgl_allocator_t*
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

/** \brief           Submit a synchronously borrowed frame and layout. */
xgl_error_t xgl_datalink_submit(xgl_datalink_ctx_t* ctx,
                                const xgl_frame_tx_message_t* message);

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
 * \brief           Expire an incomplete frame independently of PHY polling
 * \param[in,out]   ctx: Shared diagnostics and owner context
 * \param[in,out]   parser: Parser owned by the selected PHY
 * \param[in]       now_ms: Explicit monotonic time in milliseconds
 * \param[in]       timeout_ms: Partial-frame lifetime; zero disables expiry
 * \return          XGL_ERR_TIMEOUT after resetting an expired frame, else OK
 */
xgl_error_t xgl_datalink_expire_parser(xgl_datalink_ctx_t* ctx,
                                       xgl_parser_t* parser, uint32_t now_ms,
                                       uint32_t timeout_ms);

/**
 * \brief           Consume borrowed bytes synchronously using one PHY parser
 * \param[in,out]   ctx: Shared diagnostics and delivery context
 * \param[in,out]   parser: Initialized parser with exclusive receive storage
 * \param[in]       bytes: Input span; NULL is valid only when len is zero
 * \param[in]       len: Number of bytes to consume completely
 * \param[in]       current_time_ms: Explicit monotonic time in milliseconds
 * \return          First delivery/policy error, or XGL_OK; malformed frames
 *                  are discarded and counted through diagnostics
 * \note            Input must not overlap parser storage and is borrowed only
 *                  until return. No input or view pointer is retained. Partial
 *                  frames are copied into parser-owned storage. Delivery is
 *                  synchronous; callbacks must not reenter or mutate input.
 *                  Cached candidates drain before another byte is accepted;
 *                  an empty span can drain recovery after a recorded ERROR.
 *                  An initial COMPLETE is treated as already consumed. Upper
 *                  rejection consumes the whole wire-valid frame, never its
 *                  payload as new framing candidates.
 *                  This private entry does not poll the PHY or expire a parser;
 *                  its caller performs timeout maintenance before consumption.
 */
xgl_error_t xgl_datalink_consume_span(xgl_datalink_ctx_t* ctx,
                                      xgl_parser_t* parser,
                                      const uint8_t* bytes, size_t len,
                                      uint32_t current_time_ms);

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
 *                  Return the first timeout, PHY or delivery error while
 *                  consuming the entire received chunk. Malformed stream
 *                  frames are discarded and reported through diagnostics.
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
xgl_error_t xgl_datalink_get_tx_endpoint(xgl_datalink_ctx_t* ctx,
                                         xgl_frame_tx_endpoint_t* iface);

#if XGL_FEATURE_ADVANCED_RUNTIME
/**
 * \brief           Consume a byte span with resumable candidate validation
 * \param[in,out]   ctx: Shared delivery and diagnostic context
 * \param[in,out]   parser: Parser owning bounded persistent input storage
 * \param[in]       bytes: Input span, or NULL for an empty span
 * \param[in]       len: Available input bytes
 * \param[in]       now_ms: Arrival time for newly retained input bytes
 * \param[in,out]   candidates: Remaining validation checks for this link
 * \param[out]      consumed: Bytes durably retained or processed from the span
 * \param[out]      pending: True when local buffered work remains
 * \return          First delivery error, or XGL_OK
 * \note            Input must not overlap parser storage. The caller retains
 *                  any unconsumed tail. Delivery is synchronous and its view
 *                  cannot outlive the callback or reenter the instance.
 *                  A check may process a whole CRC; its byte cost is not split.
 */
xgl_error_t xgl_datalink_consume_span_bounded(xgl_datalink_ctx_t* ctx,
                                              xgl_parser_t* parser,
                                              const uint8_t* bytes, size_t len,
                                              uint32_t now_ms,
                                              size_t* candidates,
                                              size_t* consumed, bool* pending);

/**
 * \brief           Resume retained input and read at most one bounded PHY span
 * \param[in,out]   ctx: Shared delivery and diagnostic context
 * \param[in,out]   parser: Parser and persistent byte storage
 * \param[in]       phy: Synchronous physical operations
 * \param[in]       now_ms: Current monotonic time
 * \param[in]       timeout_ms: Absolute candidate lifetime, or zero
 * \param[in]       byte_budget: Maximum newly read bytes, zero skips the PHY
 * \param[in,out]   candidates: Remaining validation checks for this link
 * \param[out]      polled: True only when the PHY RX callback was invoked
 * \param[out]      pending: True when local buffered work remains
 * \return          First timeout, delivery or PHY error, or XGL_OK
 * \note            A zero validation quota retains received bytes without
 *                  validating them. No stack input tail survives the call.
 */
xgl_error_t xgl_datalink_poll_parser_bounded(
    xgl_datalink_ctx_t* ctx, xgl_parser_t* parser, xgl_phy_ops_t* phy,
    uint32_t now_ms, uint32_t timeout_ms, size_t byte_budget,
    size_t* candidates, bool* polled, bool* pending);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XGL_DATALINK_H */
