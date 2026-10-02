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

/** \brief           Protocol major version; changes can break compatibility. */
#define XGL_VERSION_MAJOR 3

/**
 * \brief           Protocol minor version
 * \note            Incremented for new features (backward compatible)
 */
#define XGL_VERSION_MINOR 0

/**
 * \brief           Protocol patch version
 * \note            Incremented for bug fixes
 */
#define XGL_VERSION_PATCH 0

/**
 * \brief           Protocol version string
 */
#define XGL_VERSION_STRING "3.0.0"

/**
 * \brief           Protocol version as integer (MAJOR * 10000 + MINOR * 100 +
 * PATCH)
 * \note            Useful for compile-time version checks
 */
#define XGL_VERSION_INT                                                        \
    ((XGL_VERSION_MAJOR * 10000) + (XGL_VERSION_MINOR * 100) +                 \
     XGL_VERSION_PATCH)

/**
 * \brief           Check if protocol version is at least the specified version
 * \param[in]       major: Major version
 * \param[in]       minor: Minor version
 * \param[in]       patch: Patch version
 * \return          1 if current version >= specified version, 0 otherwise
 */
#define XGL_VERSION_CHECK(major, minor, patch)                                 \
    (XGL_VERSION_INT >= ((major) * 10000 + (minor) * 100 + (patch)))

/**
 * \brief           Reserve a workspace after checking consumer ABI constants
 * \param[in]       config: Immutable configuration borrowed until destruction
 * \param[in]       config_size: Size of the consumer's configuration structure
 * \param[in]       abi_version: Consumer configuration ABI version
 * \param[in]       build_config_id: Consumer compiled profile identifier
 * \return          Reserved handle, or NULL for invalid input/allocation
 * failure
 * \note            Call xgl_init() before using the reserved instance.
 */
xgl_handle_t xgl_create_checked(const xgl_config_t* config, size_t config_size,
                                uint32_t abi_version, uint32_t build_config_id);

/**
 * \brief           Reserve an instance and return the exact failure reason
 * \param[in]       config: Immutable configuration borrowed until destruction
 * \param[in]       config_size: Size of the consumer configuration structure
 * \param[in]       abi_version: Consumer configuration ABI version
 * \param[in]       build_config_id: Consumer compiled profile identifier
 * \param[out]      handle: Reserved instance; cleared on every failure
 * \return          XGL_OK or a configuration, ABI, backend or allocation error
 * \note            Successful reservation still requires xgl_init().
 */
xgl_error_t xgl_create_with_error_checked(const xgl_config_t* config,
                                          size_t config_size,
                                          uint32_t abi_version,
                                          uint32_t build_config_id,
                                          xgl_handle_t* handle);

/**
 * \brief           Reserve an instance with consumer ABI checks and errors
 * \param[in]       config: Immutable configuration borrowed until destruction
 * \param[out]      handle: Reserved instance; cleared on every failure
 * \return          XGL_OK or a configuration, ABI, backend or allocation error
 * \note            Call xgl_init() before using the reserved instance.
 */
static inline xgl_error_t xgl_create_with_error(const xgl_config_t* config,
                                                xgl_handle_t* handle) {
    return xgl_create_with_error_checked(config, sizeof(xgl_config_t),
                                         XGL_CONFIG_ABI_VERSION,
                                         XGL_BUILD_CONFIG_ID, handle);
}

/**
 * \brief           Create a new protocol instance
 * \param[in]       config: Configuration structure
 * \return          Instance handle on success, NULL on failure
 * \note            The immutable configuration is borrowed until xgl_destroy()
 * \note            Use xgl_init() to initialize the instance after creation
 * \note            Memory is allocated through config.memory.allocator. When
 *                  XGL_ALLOW_FALLBACK_MALLOC is enabled, NULL allocator uses
 *                  the default malloc/free wrapper; strict embedded builds can
 *                  disable that fallback.
 * \warning         Must call xgl_destroy() to free resources
 *
 * \par Example
 * \code{.c}
 * xgl_config_t config;
 * xgl_config_get_default(&config);
 * config.source_id = 1;
 *
 * xgl_handle_t handle = xgl_create(&config);
 * if (handle == NULL) {
 *     printf("Failed to create instance\n");
 *     return -1;
 * }
 * \endcode
 */
static inline xgl_handle_t xgl_create(const xgl_config_t* config) {
    return xgl_create_checked(config, sizeof(xgl_config_t),
                              XGL_CONFIG_ABI_VERSION, XGL_BUILD_CONFIG_ID);
}

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
 * \brief           Exact reserved bytes by ownership and resource category
 * \note            Category bytes include alignment and sum to
 * requirements.size. Admission budgets do not change fixed pool reservations.
 */
typedef struct {
    xgl_memory_requirements_t requirements; /**< Complete storage contract */
    size_t management_bytes;     /**< Instance and allocator descriptors */
    size_t initialization_bytes; /**< Routes, links and per-PHY RX caches */
    size_t peer_bytes;           /**< Reliable scope records */
    size_t window_bytes;         /**< Zero; windows belong to peer_bytes */
    size_t tx_record_bytes;      /**< Outstanding reliable packet records */
    size_t tx_payload_bytes;     /**< Reserved reliable payload copies */
    size_t scratch_bytes;        /**< Synchronous frame scratch storage */
    size_t rx_buffered_bytes;    /**< Out-of-order records, payload and TLVs */
    size_t fragment_bytes;       /**< Message storage and reassembly records */
} xgl_memory_report_t;

/**
 * \brief           Query the exact static workspace layout after ABI validation
 * \details         Accounts for every enabled protocol capacity and alignment.
 *                  No heap fallback or runtime expansion is performed.
 *                  Only size is the exact total; runtime_blocks multiplied by
 *                  runtime_block_size is not a workspace size.
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[in]       config_size: Size of the caller's configuration structure
 * \param[in]       abi_version: Caller configuration ABI version
 * \param[in]       build_config_id: Caller compiled profile identifier
 * \param[out]      requirements: Required size, alignment, and runtime capacity
 * \return          XGL_OK, XGL_ERR_UNSUPPORTED, or configuration/ABI error
 */
xgl_error_t
xgl_memory_requirements_checked(const xgl_config_t* config, size_t config_size,
                                uint32_t abi_version, uint32_t build_config_id,
                                xgl_memory_requirements_t* requirements);

/**
 * \brief           Query requirements using the consumer header's ABI constants
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[out]      requirements: Required size, alignment, and runtime capacity
 * \return          XGL_OK, XGL_ERR_UNSUPPORTED, or configuration/ABI error
 */
static inline xgl_error_t
xgl_memory_requirements(const xgl_config_t* config,
                        xgl_memory_requirements_t* requirements) {
    return xgl_memory_requirements_checked(config, sizeof(xgl_config_t),
                                           XGL_CONFIG_ABI_VERSION,
                                           XGL_BUILD_CONFIG_ID, requirements);
}

/**
 * \brief           Explain reserved storage without allocating or initializing
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[in]       config_size: Size of the caller's configuration structure
 * \param[in]       abi_version: Caller configuration ABI version
 * \param[in]       build_config_id: Caller compiled profile identifier
 * \param[out]      report: Byte breakdown; unchanged on failure
 * \return          XGL_OK or a configuration, capacity or ABI error
 */
xgl_error_t xgl_memory_report_checked(const xgl_config_t* config,
                                      size_t config_size, uint32_t abi_version,
                                      uint32_t build_config_id,
                                      xgl_memory_report_t* report);

/**
 * \brief           Query reserved bytes using the consumer's ABI constants
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[out]      report: Byte breakdown; unchanged on failure
 * \return          XGL_OK or a configuration, capacity or ABI error
 */
static inline xgl_error_t xgl_memory_report(const xgl_config_t* config,
                                            xgl_memory_report_t* report) {
    return xgl_memory_report_checked(config, sizeof(xgl_config_t),
                                     XGL_CONFIG_ABI_VERSION,
                                     XGL_BUILD_CONFIG_ID, report);
}

/**
 * \brief           Initialize the protocol inside caller-owned storage
 * \note            The workspace must remain at a fixed address until destroy.
 *                  It must not overlap the configuration or referenced objects.
 *                  The config, PHY descriptors and callback contexts must
 * remain valid and configuration arrays must not be mutated while in use.
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[in]       config_size: Size of the caller's configuration structure
 * \param[in]       abi_version: Caller configuration ABI version
 * \param[in]       build_config_id: Caller compiled profile identifier
 * \param[in]       workspace: Aligned caller-owned storage
 * \param[in]       workspace_size: Available bytes from workspace
 * \param[out]      handle: Instance handle, or NULL on failure
 * \return          XGL_OK or validation/capacity error; failure clears handle
 */
xgl_error_t xgl_init_static_checked(const xgl_config_t* config,
                                    size_t config_size, uint32_t abi_version,
                                    uint32_t build_config_id, void* workspace,
                                    size_t workspace_size,
                                    xgl_handle_t* handle);

/**
 * \brief           Initialize with the consumer header's ABI constants
 * \param[in]       config: Configuration supported by the static storage plan
 * \param[in]       workspace: Aligned caller-owned storage
 * \param[in]       workspace_size: Available bytes from workspace
 * \param[out]      handle: Instance handle, or NULL on failure
 * \return          XGL_OK on success, error code otherwise
 */
static inline xgl_error_t xgl_init_static(const xgl_config_t* config,
                                          void* workspace,
                                          size_t workspace_size,
                                          xgl_handle_t* handle) {
    return xgl_init_static_checked(config, sizeof(xgl_config_t),
                                   XGL_CONFIG_ABI_VERSION, XGL_BUILD_CONFIG_ID,
                                   workspace, workspace_size, handle);
}

/**
 * \brief           Initialize the layers in a previously reserved workspace
 * \param[in]       handle: Instance returned by xgl_create()
 * \return          XGL_OK, NULL_POINTER, ALREADY_INITIALIZED, or resource error
 */
xgl_error_t xgl_init(xgl_handle_t handle);

/**
 * \brief           Release protocol resources without freeing borrowed storage
 * \param[in]       handle: Instance to destroy; NULL is ignored
 * \pre             No operation or callback may still be using the instance.
 */
void xgl_destroy(xgl_handle_t handle);

/**
 * \brief           Read the library version used at link time
 * \return          Read-only version string with static lifetime
 */
const char* xgl_version_string(void);

/**
 * \brief           Read the library version as an integer
 * \return          MAJOR * 10000 + MINOR * 100 + PATCH
 */
uint32_t xgl_version_int(void);

/**
 * \brief           Select defaults supported by the compiled profile
 * \param[out]      config: Configuration to overwrite; NULL is ignored
 */
void xgl_config_get_default(xgl_config_t* config);

/**
 * \brief           Select one peer, one TX slot, and 128-byte frames
 * \param[out]      config: Configuration to overwrite; NULL is ignored
 */
void xgl_config_get_preset_boot(xgl_config_t* config);

/**
 * \brief           Select the tiny bounded protocol configuration
 * \param[out]      config: Configuration to overwrite; NULL is ignored
 */
void xgl_config_get_preset_tiny(xgl_config_t* config);

/**
 * \brief           Select the small bounded protocol configuration
 * \param[out]      config: Configuration to overwrite; NULL is ignored
 */
void xgl_config_get_preset_small(xgl_config_t* config);

/**
 * \brief           Select the medium bounded protocol configuration
 * \param[out]      config: Configuration to overwrite; NULL is ignored
 */
void xgl_config_get_preset_medium(xgl_config_t* config);

/**
 * \brief           Select the large bounded protocol configuration
 * \param[out]      config: Configuration to overwrite; NULL is ignored
 */
void xgl_config_get_preset_large(xgl_config_t* config);

/**
 * \brief           Select authenticated defaults; a provider must be supplied
 * \param[out]      config: Configuration to overwrite; NULL is ignored
 */
void xgl_config_get_preset_production(xgl_config_t* config);

/**
 * \brief           Validate configuration and the compiled capability ceiling
 * \param[in]       config: Configuration to validate without modifying it
 * \return          XGL_OK, NULL_POINTER, INVALID_PARAM, or UNSUPPORTED
 */
xgl_error_t xgl_config_validate(const xgl_config_t* config);

/**
 * \brief           Submit data at the caller's monotonic millisecond time
 * \param[in]       handle: Initialized instance
 * \param[in]       tx_data: Payload and addressing borrowed during this call
 * \param[in]       now_ms: Same clock used by xgl_step(), wrapping modulo 2^32
 * \return          XGL_OK when accepted, or validation/capacity/driver error
 * \note            Reliable data is copied to bounded protocol-owned storage.
 *                  Successful submission does not mean remote delivery.
 */
xgl_error_t xgl_send_at(xgl_handle_t handle, const xgl_tx_data_t* tx_data,
                        uint32_t now_ms);

/**
 * \brief           Send with caller-provided synchronous frame workspace
 * \param[in]       handle: Initialized instance
 * \param[in]       tx_data: Writable frame workspace and payload description
 * \param[in]       now_ms: Same clock used by xgl_step()
 * \return          XGL_OK on acceptance, or validation/capacity/driver error
 * \note            The PHY must stop reading before tx returns. Reliable data
 *                  sends are unsupported; timeout_ms is ignored. This legacy
 *                  entry always uses connection ID and session epoch zero.
 */
xgl_error_t xgl_send_zerocopy_at(xgl_handle_t handle,
                                 const xgl_tx_data_zerocopy_t* tx_data,
                                 uint32_t now_ms);

/**
 * \brief           Query exact frame storage without transmitting or allocating
 * \param[in]       handle: Initialized instance, serialized by the caller
 * \param[in]       request: Scope and send metadata; buffer, buffer_size and
 *                  data_offset are ignored, data_len must be nonzero
 * \param[out]      layout: Exact headroom and frame size, cleared on failure
 * \return          XGL_OK or an argument, message-limit, route, MTU, security
 *                  or terminal-scope error
 * \note            Does not call the provider or PHY, reserve storage or
 * consume a security sequence. An authenticated scope must already be
 *                  installed. The eventual send revalidates current state.
 */
xgl_error_t xgl_send_zerocopy_layout(xgl_handle_t handle,
                                     const xgl_tx_zerocopy_request_t* request,
                                     xgl_tx_frame_layout_t* layout);

/**
 * \brief           Send one caller-owned frame within an explicit peer scope
 * \param[in]       handle: Initialized instance, serialized by the caller
 * \param[in]       request: Writable frame storage and exact connection/epoch
 * \param[in]       now_ms: Same monotonic clock used by xgl_step()
 * \return          XGL_OK or a validation, capacity, security or driver error
 * \note            Use xgl_send_zerocopy_layout() before filling the payload.
 *                  The payload stays at its original address. All callbacks
 *                  borrow it synchronously; no pointer may outlive this call.
 *                  Reliable sends are unsupported; timeout_ms is ignored.
 */
xgl_error_t
xgl_send_zerocopy_scoped_at(xgl_handle_t handle,
                            const xgl_tx_zerocopy_request_t* request,
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
 */
xgl_error_t xgl_step(xgl_handle_t handle, uint32_t now_ms,
                     const xgl_work_budget_t* budget);

/**
 * \brief           Query the next relative protocol or link deadline
 * \param[in]       handle: Initialized instance
 * \param[in]       now_ms: Current monotonic time
 * \param[out]      delay_ms: Relative delay; zero means work is already due
 * \return          True if a deadline exists; false leaves delay_ms unchanged
 * \note            Always includes PHY polling and the most recent step's
 *                  parser policy. Use xgl_next_timeout_for_work() when the
 *                  next step's work budget differs.
 */
bool xgl_next_timeout(xgl_handle_t handle, uint32_t now_ms, uint32_t* delay_ms);

/**
 * \brief           Query deadlines for a proposed step without changing state
 * \param[in]       handle: Initialized instance, serialized by the caller
 * \param[in]       now_ms: Same monotonic millisecond clock used by xgl_step()
 * \param[in]       budget: Proposed per-link RX budget and parser timeout
 * \param[out]      delay_ms: Relative delay; unchanged when no deadline exists
 * \return          True if a deadline exists; false for invalid input or none
 * \note            Zero rx_bytes excludes PHY polling deadlines. Parser expiry
 *                  uses this budget's receive_timeout_ms, without changing the
 *                  last step policy or restarting a partial frame. Transport
 *                  deadlines remain active. A timeout of 2^31 or more is
 * invalid.
 */
bool xgl_next_timeout_for_work(xgl_handle_t handle, uint32_t now_ms,
                               const xgl_work_budget_t* budget,
                               uint32_t* delay_ms);

/**
 * \brief           Install an explicitly trusted directional security session
 * \param[in]       handle: Initialized instance
 * \param[in]       config: Trusted session parameters copied into the instance
 * \return          XGL_OK, UNSUPPORTED, or validation/capacity error
 * \note            The application must prevent key/nonce-domain reuse across
 *                  restarts using persistent state or fresh trusted keys.
 *                  Ordinary received frames cannot install sessions.
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

/**
 * \brief           Cancel a peer's pending TX and release its capacity
 * \param[in]       handle: Initialized instance
 * \param[in]       remote_id: Peer identifier
 * \param[in]       connection_id: Connection identifier
 * \param[in]       session_epoch: Exact peer epoch to release
 * \return          XGL_OK, NOT_FOUND, or validation error
 * \note            For authenticated peers, close the security session instead.
 *                  Otherwise the caller must drain old link traffic and choose
 *                  a new epoch before reconnecting. Pending TX reports
 * CANCELLED.
 */
xgl_error_t xgl_close_peer(xgl_handle_t handle, uint16_t remote_id,
                           uint32_t connection_id, uint32_t session_epoch);

/**
 * \brief           Copy counters under caller-serialized instance access
 * \param[in]       handle: Initialized instance
 * \param[out]      stats: Snapshot destination; unchanged when unsupported
 * \return          XGL_OK, XGL_ERR_NULL_POINTER, XGL_ERR_NOT_INITIALIZED, or
 *                  XGL_ERR_UNSUPPORTED when statistics are compiled out
 */
xgl_error_t xgl_stats_get(xgl_handle_t handle, xgl_statistics_t* stats);

/**
 * \brief           Reset observations while preserving the workspace
 * reservation
 * \param[in]       handle: Initialized instance
 * \return          XGL_OK, XGL_ERR_NULL_POINTER, XGL_ERR_NOT_INITIALIZED, or
 *                  XGL_ERR_UNSUPPORTED when statistics are compiled out
 */
xgl_error_t xgl_stats_reset(xgl_handle_t handle);

/**
 * \brief           Measure independent message storage with versioned policy
 * \param[in]       config: Immutable instance configuration
 * \param[in]       options: Initialized versioned policy
 * \param[out]      report: Exact reservation, unchanged on failure
 * \return          XGL_OK, validation error, or UNSUPPORTED without capability
 */
xgl_error_t
xgl_memory_report_with_options(const xgl_config_t* config,
                               const xgl_instance_options_t* options,
                               xgl_memory_report_t* report);

/**
 * \brief           Reserve an instance borrowing versioned optional policy
 * \param[in]       config: Immutable configuration borrowed until destroy
 * \param[in]       options: Immutable policy borrowed until destroy
 * \param[out]      handle: Reserved instance, cleared on failure
 * \return          XGL_OK or validation/allocation error; call xgl_init next
 */
xgl_error_t xgl_create_with_options(const xgl_config_t* config,
                                    const xgl_instance_options_t* options,
                                    xgl_handle_t* handle);

/**
 * \brief           Initialize caller storage with explicit optional policy
 * \param[in]       config: Immutable configuration borrowed until destroy
 * \param[in]       options: Immutable policy borrowed until destroy
 * \param[in,out]   storage: Aligned caller-owned workspace
 * \param[in]       storage_size: Available bytes from the options-aware report
 * \param[out]      handle: Initialized instance, cleared on failure
 * \return          XGL_OK or validation, storage or initialization error
 */
xgl_error_t xgl_init_static_with_options(const xgl_config_t* config,
                                         const xgl_instance_options_t* options,
                                         void* storage, size_t storage_size,
                                         xgl_handle_t* handle);

/**
 * \brief           Run a step with additional candidate and TX-attempt limits
 * \param[in,out]   handle: Caller-serialized initialized instance
 * \param[in]       now_ms: Same monotonic clock as all instance operations
 * \param[in]       budget: Per-PHY read and parser timeout policy
 * \param[in]       limits: Per-link candidate and whole-maintenance TX limits
 * \param[out]      result: Continuation flag, initialized even on failure
 * \return          First processing error, or XGL_OK; UNSUPPORTED if disabled
 * \note            Timers still run with zero limits. Callback time, a single
 *                  CRC, receive ACKs and application send calls are not bounded
 *                  by these limits. Pending work requires another step with
 *                  positive limits; application BUSY requires an external wake.
 */
xgl_error_t xgl_step_bounded(xgl_handle_t handle, uint32_t now_ms,
                             const xgl_work_budget_t* budget,
                             const xgl_execution_limits_t* limits,
                             xgl_execution_result_t* result);

/**
 * \brief           Inspect currently owned resources without allocation
 * \param[in]       handle: Caller-serialized initialized instance
 * \param[out]      snapshot: Current resource counts, unchanged on error
 * \return          XGL_OK, argument error, or UNSUPPORTED if disabled
 */
xgl_error_t xgl_resources_get(xgl_handle_t handle,
                              xgl_resource_snapshot_t* snapshot);

#ifdef __cplusplus
}
#endif

#endif /* XGL_H */
