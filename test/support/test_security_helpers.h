/**
 * \file            test_security_helpers.h
 * \brief           Explicit trusted fixtures for wire and frame tests
 */
#ifndef TEST_SECURITY_HELPERS_H
#define TEST_SECURITY_HELPERS_H
#include <security/xgl_security.h>
#include <wire/xgl_frame.h>

/** \brief           Build a deterministic trusted test association. */
static inline xgl_security_session_config_t
test_session_config(uint16_t remote, uint32_t connection = 0U,
                    uint32_t epoch = 0U, uint32_t key = 7U,
                    bool receiver = false) {
    xgl_security_session_config_t config = {};
    config.remote_id = remote;
    config.connection_id = connection;
    config.session_epoch = epoch;
    config.tx_key_id = key;
    config.rx_key_id = key;
    config.tx_nonce_prefix = receiver ? 2U : 1U;
    config.rx_nonce_prefix = receiver ? 1U : 2U;
    return config;
}

/** \brief           Serialize once using an explicitly installed test session.
 */
static inline xgl_error_t test_serialize_trusted_frame(
    uint8_t* buffer, size_t capacity, const xgl_frame_t* frame, uint32_t key,
    const xgl_auth_provider_t* provider, size_t* written) {
    xgl_security_ctx_t security = {};
    xgl_error_t err =
        xgl_security_init(&security, frame->header.source_id, true, provider);
    if (err != XGL_OK) {
        return err;
    }
    xgl_wire_ext_metadata_t metadata = {};
    err = xgl_wire_decode_ext_metadata(frame->extensions, frame->extensions_len,
                                       &metadata);
    if (err != XGL_OK) {
        return err;
    }
    auto config = test_session_config(frame->header.target_id,
                                      frame->header.connection_id,
                                      metadata.session_epoch, key);
    err = xgl_security_session_install(&security, &config);
    return err == XGL_OK ? xgl_security_serialize_frame(buffer, capacity, frame,
                                                        &security, written)
                         : err;
}

/** \brief           Verify once using an explicitly installed receiving
 * session. */
static inline xgl_error_t
test_verify_trusted_frame(const uint8_t* buffer, size_t length_without_crc,
                          size_t, size_t, uint32_t key,
                          const xgl_auth_provider_t* provider, bool* valid) {
    *valid = false;
    xgl_wire_frame_view_t view = {};
    xgl_error_t err = xgl_wire_decode_frame(
        &view, buffer, length_without_crc + XGL_CRC16_SIZE, nullptr);
    if (err != XGL_OK) {
        return err;
    }
    xgl_security_ctx_t security = {};
    err = xgl_security_init(&security, view.header.target_id, true, provider);
    if (err != XGL_OK) {
        return err;
    }
    auto config =
        test_session_config(view.header.source_id, view.header.connection_id,
                            view.session_epoch, key, true);
    err = xgl_security_session_install(&security, &config);
    if (err != XGL_OK) {
        return err;
    }
    *valid = xgl_security_verify_frame(&security, &view) == XGL_OK;
    return XGL_OK;
}
#endif
