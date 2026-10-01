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
 *                  sends are unsupported by this zero-copy entry point.
 */
xgl_error_t xgl_send_zerocopy_at(xgl_handle_t handle,
                                 const xgl_tx_data_zerocopy_t* tx_data,
                                 uint32_t now_ms);

/**
 * \brief           Poll due links and advance protocol deadlines
 * \param[in]       handle: Initialized instance
 * \param[in]       now_ms: Monotonic time; elapsed intervals must stay below
 * 2^31
 * \param[in]       budget: Per-link RX byte budget and parser timeout
 * \return          First receive or transport error, or XGL_OK
 */
xgl_error_t xgl_step(xgl_handle_t handle, uint32_t now_ms,
                     const xgl_work_budget_t* budget);

/**
 * \brief           Query the next relative protocol or link deadline
 * \param[in]       handle: Initialized instance
 * \param[in]       now_ms: Current monotonic time
 * \param[out]      delay_ms: Relative delay; zero means work is already due
 * \return          True if a deadline exists; false leaves delay_ms unchanged
 */
bool xgl_next_timeout(xgl_handle_t handle, uint32_t now_ms, uint32_t* delay_ms);

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
 * \param[out]      stats: Destination for a snapshot of all counters
 * \return          XGL_OK, XGL_ERR_NULL_POINTER, or XGL_ERR_NOT_INITIALIZED
 */
xgl_error_t xgl_stats_get(xgl_handle_t handle, xgl_statistics_t* stats);

/**
 * \brief           Clear all counters under caller-serialized instance access
 * \param[in]       handle: Initialized instance
 * \return          XGL_OK, XGL_ERR_NULL_POINTER, or XGL_ERR_NOT_INITIALIZED
 */
xgl_error_t xgl_stats_reset(xgl_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* XGL_H */
