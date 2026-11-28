/**
 * \file            xgl.h
 * \brief           Portable synchronous protocol API
 * \author          X-Gen Lab
 * \details         Calls on the same instance must be serialized by the caller.
 *                  Callbacks execute synchronously and must not reenter it.
 *                  Configuration, PHY descriptors, providers and their contexts
 *                  are borrowed and must remain valid until destruction.
 */

#ifndef XGL_H
#define XGL_H

#include <xgl/xgl_config.h>
#include <xgl/xgl_error.h>
#include <xgl/xgl_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief           SDK version as integer (MAJOR * 10000 + MINOR * 100 +
 * PATCH)
 * \note            Useful for compile-time version checks
 */
#define XGL_VERSION_INT                                                        \
    ((XGL_VERSION_MAJOR * 10000) + (XGL_VERSION_MINOR * 100) +                 \
     XGL_VERSION_PATCH)

/**
 * \brief           Check if SDK version is at least the specified version
 * \param[in]       major: Major version
 * \param[in]       minor: Minor version
 * \param[in]       patch: Patch version
 * \return          1 if current version >= specified version, 0 otherwise
 */
#define XGL_VERSION_CHECK(major, minor, patch)                                 \
    (XGL_VERSION_INT >= ((major)*10000 + (minor)*100 + (patch)))

/**
 * \brief           Storage requirements for a static protocol instance
 */
typedef struct {
    size_t size; /**< Total caller storage, including instance and all pools */
    size_t alignment;      /**< Required storage address alignment */
    size_t runtime_blocks; /**< Sum of reusable slots across the typed pools */
    size_t runtime_block_size; /**< Largest aligned runtime slot; pools have
                                  different strides */
} xgl_memory_requirements_t;

/**
 * \brief           Measure storage for the supplied configuration
 * \details         Accounts for every enabled protocol capacity and alignment.
 *                  No heap fallback or runtime expansion is performed.
 *                  Only size is the exact total; runtime_blocks multiplied by
 *                  runtime_block_size is not a workspace size.
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[out]      requirements: Required size, alignment, and runtime capacity
 * \return          XGL_OK, XGL_ERR_UNSUPPORTED, or a configuration error
 */
xgl_error_t xgl_memory_requirements(const xgl_config_t* config,
                                    xgl_memory_requirements_t* requirements);

/**
 * \brief           Initialize the protocol inside caller-owned storage
 * \note            The workspace must remain at a fixed address until destroy.
 *                  It must not overlap the configuration or referenced objects.
 *                  The config, PHY descriptors and callback contexts must
 * remain valid and configuration arrays must not be mutated while in use.
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[in]       workspace: Aligned caller-owned storage
 * \param[in]       workspace_size: Available bytes from workspace
 * \param[out]      handle: Instance handle, or NULL on failure
 * \return          XGL_OK or validation/capacity error; failure clears handle
 */
xgl_error_t xgl_init_static(const xgl_config_t* config, void* workspace,
                            size_t workspace_size, xgl_handle_t* handle);

/**
 * \brief           Release the instance and its protocol resources
 * \param[in]       handle: Instance to destroy; NULL is ignored
 * \pre             No operation or callback may still be using the instance.
 * \note            Caller storage can be reused after return. Owned storage
 *                  is freed through the original allocator.
 */
void xgl_destroy(xgl_handle_t handle);

/**
 * \brief           Submit one DATA frame at the caller's monotonic time
 * \param[in]       handle: Initialized instance
 * \param[in]       tx_data: Payload and addressing borrowed during this call
 * \param[in]       now_ms: Same clock used by xgl_step(), wrapping modulo 2^32
 * \return          XGL_OK when accepted, or validation/capacity/driver error
 * \note            Reliable data is copied to bounded protocol-owned storage.
 *                  Successful submission describes local acceptance.
 * \note            A payload that exceeds the frame budget returns
 *                  XGL_ERR_BUFFER_TOO_SMALL.
 */
xgl_error_t xgl_send_at(xgl_handle_t handle, const xgl_tx_data_t* tx_data,
                        uint32_t now_ms);

/**
 * \brief           Poll due links and advance protocol deadlines
 * \param[in]       handle: Initialized instance
 * \param[in]       now_ms: Monotonic time; elapsed intervals must stay below
 * 2^31
 * \param[in]       budget: Per-link RX byte budget and parser timeout
 * \return          First receive or maintenance error, or XGL_OK; remaining
 *                  bytes and due maintenance are still processed
 * \note            The most recent valid receive_timeout_ms applies to every
 *                  incomplete parser; zero disables expiry. Timeout maintenance
 *                  runs even when RX is disabled or a link is not due to poll.
 * \note            Retained transport work that encounters BUSY/NO_MEMORY
 *                  remains owned for retry and is not a maintenance error.
 *                  Immediate receive or ACK transmission errors are returned.
 * \note            Local DATA requires a receive callback. Reliable DATA also
 *                  requires a usable local feedback route before acceptance.
 */
xgl_error_t xgl_step(xgl_handle_t handle, uint32_t now_ms,
                     const xgl_work_budget_t* budget);

/**
 * \brief           Cancel and release one unprotected peer scope
 * \param[in]       handle: Initialized instance
 * \param[in]       remote_id: Peer identifier
 * \param[in]       connection_id: Connection identifier
 * \param[in]       session_epoch: Exact peer epoch to release
 * \return          XGL_OK, NOT_FOUND, or validation error
 * \note            For authenticated peers, close the security session instead.
 *                  Otherwise the caller must drain old link traffic and choose
 *                  a new epoch before reconnecting. Pending TX and retained RX
 * are released. Cancellation is observable through configured callbacks.
 */
xgl_error_t xgl_close_peer(xgl_handle_t handle, uint16_t remote_id,
                           uint32_t connection_id, uint32_t session_epoch);

#ifdef __cplusplus
}
#endif

#endif /* XGL_H */
