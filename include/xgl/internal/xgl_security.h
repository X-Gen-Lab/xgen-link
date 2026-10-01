/**
 * \file            xgl_security.h
 * \brief           Security helpers for authenticated production transport
 */

#ifndef XGL_SECURITY_H
#define XGL_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "xgl/internal/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_error.h"

typedef struct {
    uint16_t source_id;
    uint32_t connection_id;
    uint32_t session_epoch;
    uint64_t largest_packet_number;
    uint64_t received_bitmap;
    uint8_t window_size;
    bool has_largest;
} xgl_replay_window_t;

typedef enum {
    XGL_REPLAY_REJECT = 0,
    XGL_REPLAY_ACCEPT_NEW = 1,
    XGL_REPLAY_ACCEPT_DUPLICATE = 2
} xgl_replay_result_t;

xgl_error_t xgl_replay_window_init(xgl_replay_window_t* window,
                                   uint16_t source_id, uint32_t connection_id,
                                   uint32_t session_epoch, uint8_t window_size);

bool xgl_replay_window_accept(xgl_replay_window_t* window, uint16_t source_id,
                              uint32_t connection_id, uint32_t session_epoch,
                              uint64_t packet_number);

xgl_replay_result_t xgl_replay_window_check(xgl_replay_window_t* window,
                                            uint16_t source_id,
                                            uint32_t connection_id,
                                            uint32_t session_epoch,
                                            uint64_t packet_number);

#if XGL_FEATURE_AUTH
/** \brief           One explicitly trusted directional security association. */
typedef struct {
    xgl_security_session_config_t config;
    xgl_replay_window_t replay;
    uint64_t tx_next_seq;
    bool used;
    bool active;
    bool exhausted;
} xgl_security_session_t;

/** \brief           Instance security state; all access is synchronous. */
typedef struct {
    uint16_t local_id;
    bool auth_required;
    const xgl_auth_provider_t* provider;
    xgl_security_session_t sessions[XGL_SECURITY_SESSION_CAPACITY];
    uint8_t aad_scratch[UINT8_MAX];
    bool busy;
} xgl_security_ctx_t;

/**
 * \brief           Initialize security without trusting any remote session
 * \param[out]      ctx: Security context
 * \param[in]       local_id: Local endpoint identifier
 * \param[in]       auth_required: Reject unauthenticated local deliveries
 * \param[in]       provider: Borrowed provider, valid for context lifetime
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_security_init(xgl_security_ctx_t* ctx, uint16_t local_id,
                              bool auth_required,
                              const xgl_auth_provider_t* provider);

/**
 * \brief           Install a trusted association; closed slots stay reserved
 * \param[in,out]   ctx: Security context
 * \param[in]       config: Trusted values copied into the context
 * \return          XGL_OK, BUSY, NO_MEMORY or INVALID_PARAM
 */
xgl_error_t
xgl_security_session_install(xgl_security_ctx_t* ctx,
                             const xgl_security_session_config_t* config);

/**
 * \brief           Close an association without making its nonce domain
 * reusable
 * \param[in,out]   ctx: Security context
 * \param[in]       remote_id: Remote endpoint identifier
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Trusted epoch
 * \return          XGL_OK, BUSY or NOT_FOUND
 */
xgl_error_t xgl_security_session_close(xgl_security_ctx_t* ctx,
                                       uint16_t remote_id,
                                       uint32_t connection_id,
                                       uint32_t session_epoch);

/**
 * \brief           Authenticate a locally addressed validated frame and commit
 * replay
 * \param[in,out]   ctx: Security context
 * \param[in]       view: Borrowed frame validated by the wire decoder
 * \return          XGL_OK on acceptance, error code otherwise
 */
xgl_error_t xgl_security_verify_frame(xgl_security_ctx_t* ctx,
                                      const xgl_wire_frame_view_t* view);

/**
 * \brief           Allocate a fresh sequence and sign a prepared frame
 * \param[in,out]   ctx: Security context
 * \param[in,out]   buffer: Header/TLV/payload with a SECURITY placeholder
 * \param[in]       capacity: Available bytes, excluding final frame CRC
 * \param[in]       aad_len: Complete encoded header length
 * \param[in]       payload_len: Encoded payload length
 * \param[out]      frame_len: Header, payload and tag length on success
 * \return          XGL_OK on success; failed provider calls still consume
 * sequence
 */
xgl_error_t xgl_security_sign_frame(xgl_security_ctx_t* ctx, uint8_t* buffer,
                                    size_t capacity, size_t aad_len,
                                    size_t payload_len, size_t* frame_len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XGL_SECURITY_H */
