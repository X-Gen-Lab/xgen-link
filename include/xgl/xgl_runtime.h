/**
 * \file            xgl_runtime.h
 * \brief           Bounded maintenance and runtime policy
 * \author          X-Gen Lab
 */

#ifndef XGL_RUNTIME_H
#define XGL_RUNTIME_H

#include <xgl/xgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \brief           Observable scope transitions; ACK is not business
 * completion. */
typedef enum {
    XGL_SCOPE_FAILED,    /**< Terminal failure or explicit cancellation */
    XGL_SCOPE_TX_DRAINED /**< All accepted reliable TX has been acknowledged */
} xgl_scope_event_t;

/**
 * \brief           Observe a scope transition synchronously without reentry
 * \param[in]       handle: Serialized instance
 * \param[in]       remote_id: Remote endpoint
 * \param[in]       connection_id: Exact connection
 * \param[in]       session_epoch: Exact generation
 * \param[in]       event: Transition kind
 * \param[in]       error: Terminal reason, or XGL_OK for drained TX
 * \param[in]       user_data: Application context
 */
typedef void (*xgl_scope_event_fn)(xgl_handle_t handle, uint16_t remote_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch,
                                   xgl_scope_event_t event, xgl_error_t error,
                                   void* user_data);

/**
 * \brief           Versioned optional instance policy, borrowed until destroy
 * \note            Initialize with XGL_INSTANCE_OPTIONS_INIT. Requires the
 *                  advanced_runtime build capability. Scope callbacks cannot
 *                  reenter the instance.
 */
struct xgl_instance_options {
    size_t size;        /**< Exact descriptor size */
    uint32_t version;   /**< XGL_INSTANCE_OPTIONS_VERSION */
    size_t config_size; /**< Consumer xgl_config_t size */
    uint32_t
        tx_lifetime_ms; /**< Reliable TX busy-period limit; 0 = unlimited */
    bool protect_installed_scopes; /**< Require authenticated RX/TX for
                                      installed scopes */
    xgl_scope_event_fn
        scope_event_callback; /**< Optional transition observer */
    void* user_data;          /**< Borrowed callback context */
};

/** \brief           Version of the optional instance policy descriptor. */
#define XGL_INSTANCE_OPTIONS_VERSION 5U

/** \brief           C11 and C++17 initializer for optional policy. */
#define XGL_INSTANCE_OPTIONS_INIT                                              \
    {                                                                          \
        sizeof(xgl_instance_options_t), XGL_INSTANCE_OPTIONS_VERSION,          \
            sizeof(xgl_config_t), 0U, false, NULL, NULL                        \
    }

/** \brief           Additional limits for one bounded step; zero stops that
 * work. */
typedef struct {
    size_t parser_candidates; /**< Candidate checks per PHY; SIZE_MAX =
                                 unlimited */
    size_t tx_packets; /**< Maintenance TX attempts; SIZE_MAX = unlimited */
} xgl_execution_limits_t;

/** \brief           Bounded-step continuation, independent of the first error.
 */
typedef struct {
    bool pending; /**< Executable local work remains because a limit was reached
                   */
} xgl_execution_result_t;

/**
 * \brief           Query the next protocol or link deadline without mutation
 * \param[in]       handle: Initialized instance under serialized access
 * \param[in]       now_ms: Same monotonic millisecond clock used by xgl_step()
 * \param[in]       budget: Proposed work policy, or NULL to use the last valid
 *                  parser policy and include all PHY polling deadlines
 * \param[out]      delay_ms: Relative delay, zero for work already due
 * \return          True if a deadline exists. False leaves delay_ms unchanged.
 * \note            An explicit zero RX budget excludes PHY polling. Transport
 *                  deadlines and applicable parser deadlines remain active.
 */
bool xgl_deadline_query(xgl_handle_t handle, uint32_t now_ms,
                        const xgl_work_budget_t* budget, uint32_t* delay_ms);

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

#ifdef __cplusplus
}
#endif

#endif /* XGL_RUNTIME_H */
