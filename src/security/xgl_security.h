/**
 * \file            xgl_security.h
 * \brief           Security helpers for authenticated production transport
 * \author          X-Gen Lab
 */

#ifndef XGL_SECURITY_INTERNAL_H
#define XGL_SECURITY_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "wire/xgl_frame.h"
#include "wire/xgl_wire.h"
#include "xgl/xgl_config.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_security.h"

#ifdef __cplusplus
extern "C" {
#endif

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
#if XGL_FEATURE_ADVANCED_RUNTIME
    bool protect_installed_scopes;
#endif
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
 * \brief           Query authentication policy without changing trusted state
 * \param[in]       ctx: Security context, or NULL for no authentication policy
 * \param[in]       remote_id: Remote endpoint identifier
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Exact trusted epoch
 * \return          True for global protection or an explicitly protected scope
 * \note            Closed associations retain protection against plaintext.
 */
#if XGL_FEATURE_ADVANCED_RUNTIME
bool xgl_security_scope_requires_auth(const xgl_security_ctx_t* ctx,
                                      uint16_t remote_id,
                                      uint32_t connection_id,
                                      uint32_t session_epoch);
#else
static inline bool
xgl_security_scope_requires_auth(const xgl_security_ctx_t* ctx,
                                 uint16_t remote_id, uint32_t connection_id,
                                 uint32_t session_epoch) {
    (void)remote_id;
    (void)connection_id;
    (void)session_epoch;
    return ctx != NULL && ctx->auth_required;
}
#endif

/**
 * \brief           Check room for successive signatures without reserving them
 * \param[in]       ctx: Security context
 * \param[in]       remote_id: Remote endpoint identifier
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Exact trusted epoch
 * \param[in]       need_second_signature: Whether both HELLO and DATA need
 * signatures
 */
xgl_error_t xgl_security_check_tx_scope(const xgl_security_ctx_t* ctx,
                                        uint16_t remote_id,
                                        uint32_t connection_id,
                                        uint32_t session_epoch,
                                        bool need_second_signature);

/** \brief           Check whether an exact installed association is active. */
bool xgl_security_session_is_active(const xgl_security_ctx_t* ctx,
                                    uint16_t remote_id, uint32_t connection_id,
                                    uint32_t session_epoch);

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

/**
 * \brief           Sign after encoding the current measured frame
 * \param[in,out]   ctx: Non-busy security context with a valid provider
 * \param[in,out]   buffer: Encoded header, extensions and payload with tag
 * space
 * \param[in]       frame: Unchanged logical input used to encode buffer
 * \param[in]       layout: Validated layout with a nonzero provider tag length
 * \return          XGL_OK, or a source, session or provider error
 * \note            The encoder has already appended SECURITY. This continuation
 *                  borrows only the current synchronous serialization state.
 */
xgl_error_t xgl_security_sign_measured(xgl_security_ctx_t* ctx, uint8_t* buffer,
                                       const xgl_frame_t* frame,
                                       const xgl_frame_layout_t* layout);

/**
 * \brief           Resolve the single authentication policy used by TX layers
 * \param[in]       required: Instance authentication policy
 * \param[in]       provider: Borrowed authentication provider, or NULL
 * \param[in]       flags: Logical frame flags
 * \param[out]      tag_len: Valid output for the planned trailer size
 * \return          XGL_OK or XGL_ERR_INVALID_PARAM for an unusable provider
 */
xgl_error_t xgl_security_frame_tag_len(bool required,
                                       const xgl_auth_provider_t* provider,
                                       uint8_t flags, size_t* tag_len);

/**
 * \brief           Encode and authenticate a frame through one wire layout
 * \param[out]      buffer: Destination frame storage
 * \param[in]       buffer_size: Available bytes
 * \param[in]       frame: Borrowed frame fields, extensions and payload
 * \param[in,out]   security: Borrowed trusted session state
 * \param[out]      bytes_written: Complete frame length on success
 * \return          XGL_OK, or an encoding, provider or session error
 */
xgl_error_t xgl_security_serialize_frame(uint8_t* buffer, size_t buffer_size,
                                         const xgl_frame_t* frame,
                                         xgl_security_ctx_t* security,
                                         size_t* bytes_written);

/**
 * \brief           Continue a measured synchronous authenticated send
 * \param[out]      buffer: Non-NULL storage with at least layout->frame_len
 * bytes
 * \param[in]       frame: Unchanged borrowed input passed to xgl_frame_measure
 * \param[in]       layout: Successful measurement with the provider tag length
 * \param[in,out]   security: Non-NULL trusted association and provider
 * \param[out]      bytes_written: Complete frame length, zero on failure
 * \return          XGL_OK, or an encoding, provider or session error
 * \note            Private continuation; frame, spans, provider and layout must
 *                  remain unchanged throughout the current send call.
 */
xgl_error_t xgl_security_serialize_measured(uint8_t* buffer,
                                            const xgl_frame_t* frame,
                                            const xgl_frame_layout_t* layout,
                                            xgl_security_ctx_t* security,
                                            size_t* bytes_written);

#endif /* XGL_FEATURE_AUTH */

#ifdef __cplusplus
}
#endif

#endif /* XGL_SECURITY_INTERNAL_H */
