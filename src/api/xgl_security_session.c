/**
 * \file            xgl_security_session.c
 * \brief           Explicit trusted security session lifecycle
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>

#include "xgl_instance_internal.h"

/** \brief           Install trusted parameters in the instance security state.
 */
xgl_error_t
xgl_install_security_session(xgl_handle_t handle,
                             const xgl_security_session_config_t* config) {
    if (handle == NULL || config == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
#if XGL_FEATURE_AUTH
    return xgl_security_session_install(&handle->security, config);
#else
    return XGL_ERR_UNSUPPORTED;
#endif
}

/** \brief           Close a session without allowing nonce-domain reuse. */
xgl_error_t xgl_close_security_session(xgl_handle_t handle, uint16_t remote_id,
                                       uint32_t connection_id,
                                       uint32_t session_epoch) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
#if XGL_FEATURE_AUTH
    xgl_error_t error = xgl_security_session_close(
        &handle->security, remote_id, connection_id, session_epoch);
    if (error != XGL_OK) {
        return error;
    }
    error = xgl_transport_close_scope(&handle->layers.transport_ctx, handle,
                                      remote_id, connection_id, session_epoch);
    return error == XGL_ERR_NOT_FOUND ? XGL_OK : error;
#else
    (void)remote_id;
    (void)connection_id;
    (void)session_epoch;
    return XGL_ERR_UNSUPPORTED;
#endif
}

/** \brief           Explicitly cancel a peer and release its bounded resources.
 */
xgl_error_t xgl_close_peer(xgl_handle_t handle, uint16_t remote_id,
                           uint32_t connection_id, uint32_t session_epoch) {
    if (handle == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (!handle->initialized) {
        return XGL_ERR_NOT_INITIALIZED;
    }
#if XGL_FEATURE_AUTH
    const xgl_security_ctx_t* security = &handle->security;
    for (size_t i = 0U; i < XGL_SECURITY_SESSION_CAPACITY; ++i) {
        const xgl_security_session_t* session = &security->sessions[i];
        if (session->active && session->config.remote_id == remote_id &&
            session->config.connection_id == connection_id &&
            session->config.session_epoch == session_epoch) {
            return XGL_ERR_INVALID_PARAM;
        }
    }
#endif
    return xgl_transport_close_scope(&handle->layers.transport_ctx, handle,
                                     remote_id, connection_id, session_epoch);
}
