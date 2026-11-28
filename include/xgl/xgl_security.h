/**
 * \file            xgl_security.h
 * \brief           Trusted sessions and synchronous authentication
 * \author          X-Gen Lab
 */

#ifndef XGL_SECURITY_H
#define XGL_SECURITY_H

#include <xgl/xgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Version of the provider input contract; independent from the wire version.
 */
#define XGL_AUTH_INPUT_VERSION 1U
/** Canonical directional nonce length in bytes. */
#define XGL_AUTH_NONCE_SIZE 12U

/**
 * \brief           Trusted bidirectional association installed by the
 * application
 * \note            Prefixes must be unique for each direction under a key.
 *                  The application must establish freshness across restarts;
 *                  reinstalling fixed parameters after RAM loss is not secure.
 */
typedef struct {
    uint16_t remote_id;       /**< Trusted remote node identifier */
    uint32_t connection_id;   /**< Application association identifier */
    uint32_t session_epoch;   /**< Trusted association generation */
    uint32_t tx_key_id;       /**< Provider key identity for outgoing frames */
    uint32_t rx_key_id;       /**< Provider key identity for incoming frames */
    uint32_t tx_nonce_prefix; /**< Unique outgoing nonce domain under its key */
    uint32_t rx_nonce_prefix; /**< Unique incoming nonce domain under its key */
    uint64_t tx_initial_seq;  /**< First outgoing security sequence */
    uint64_t rx_min_seq; /**< Lowest acceptable incoming security sequence */
} xgl_security_session_config_t;

/**
 * \brief           Versioned authentication vector derived from actual wire
 * bytes
 * \note            Nonce is BE32(installed directional prefix) followed by
 *                  BE64(security_seq). AAD contains the complete header/TLVs
 * with TTL and header CRC bytes zeroed. Providers must authenticate every AAD
 * and payload byte using this nonce and key identity. All pointers are borrowed
 * for the callback only. Providers are synchronous and must not reenter the
 * protocol instance.
 */
typedef struct {
    uint32_t version;       /**< Provider input contract version */
    uint8_t wire_version;   /**< Encoded protocol version */
    uint16_t source_id;     /**< Encoded sender identifier */
    uint16_t target_id;     /**< Encoded recipient identifier */
    uint32_t connection_id; /**< Encoded association identifier */
    uint32_t session_epoch; /**< Encoded trusted association generation */
    uint32_t key_id;        /**< Installed directional provider key identity */
    uint64_t security_seq;  /**< Per-direction sequence used in the nonce */
    uint8_t nonce[XGL_AUTH_NONCE_SIZE]; /**< Canonical authentication nonce */
    const uint8_t* aad; /**< Borrowed canonical header and extension bytes */
    size_t aad_len;     /**< Number of authenticated header bytes */
    const uint8_t* payload; /**< Borrowed payload bytes */
    size_t payload_len;     /**< Number of authenticated payload bytes */
} xgl_auth_input_t;

/**
 * \brief           Sign the complete versioned authentication vector
 * \param[in]       input: Validated, canonical protocol input
 * \param[out]      tag: Tag output buffer, borrowed for this call
 * \param[in]       tag_capacity: Available tag bytes
 * \param[out]      tag_len: Actual tag length; must equal provider.tag_len
 * \param[in]       user_data: Application provider context
 * \return          XGL_OK on success, error code otherwise
 * \note            The reserved security sequence is consumed even on failure.
 */
typedef xgl_error_t (*xgl_auth_sign_fn)(const xgl_auth_input_t* input,
                                        uint8_t* tag, size_t tag_capacity,
                                        size_t* tag_len, void* user_data);

/**
 * \brief           Verify a tag against the complete authentication vector
 * \param[in]       input: Validated, canonical protocol input
 * \param[in]       tag: Borrowed received tag bytes
 * \param[in]       tag_len: Expected fixed tag length
 * \param[out]      valid: True only after successful cryptographic verification
 * \param[in]       user_data: Application provider context
 * \return          XGL_OK when verification completed, error code otherwise
 */
typedef xgl_error_t (*xgl_auth_verify_fn)(const xgl_auth_input_t* input,
                                          const uint8_t* tag, size_t tag_len,
                                          bool* valid, void* user_data);

/** \brief           Synchronous application-supplied authentication algorithm
 */
struct xgl_auth_provider {
    xgl_auth_sign_fn sign; /**< Synchronous outgoing authentication callback */
    xgl_auth_verify_fn
        verify;      /**< Synchronous incoming verification callback */
    size_t tag_len;  /**< Fixed authentication tag size in bytes */
    void* user_data; /**< Borrowed application context passed to callbacks */
};

/**
 * \brief           Install an explicitly trusted directional security session
 * \param[in]       handle: Initialized instance
 * \param[in]       config: Trusted session parameters copied into the instance
 * \return          XGL_OK, UNSUPPORTED, or validation/capacity error
 * \note            The application must prevent key/nonce-domain reuse across
 *                  restarts using persistent state or fresh trusted keys.
 *                  Ordinary received frames cannot install sessions.
 *                  Installation does not revoke previously admitted data.
 *                  Use a new trusted scope for a business trust transition.
 */
xgl_error_t
xgl_install_security_session(xgl_handle_t handle,
                             const xgl_security_session_config_t* config);

/**
 * \brief           Close a session while retaining its nonce-domain tombstone
 * \param[in]       handle: Initialized instance
 * \param[in]       remote_id: Peer identifier
 * \param[in]       connection_id: Connection identifier
 * \param[in]       session_epoch: Installed trusted epoch
 * \return          XGL_OK, UNSUPPORTED, NOT_FOUND, or validation error
 */
xgl_error_t xgl_close_security_session(xgl_handle_t handle, uint16_t remote_id,
                                       uint32_t connection_id,
                                       uint32_t session_epoch);

#ifdef __cplusplus
}
#endif

#endif /* XGL_SECURITY_H */
