/**
 * \file            xgl_security_frame.c
 * \brief           Trusted authentication orchestration over the wire codec
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_security.h>

/** \brief           Resolve authentication overhead before reserving storage.
 */
xgl_error_t xgl_security_frame_tag_len(bool required,
                                       const xgl_auth_provider_t* provider,
                                       uint8_t flags, size_t* tag_len) {
    *tag_len = 0U;
    if (!required && (flags & XGL_WIRE_FLAG_AUTHENTICATED) == 0U) {
        return XGL_OK;
    }
    if (provider == NULL || provider->sign == NULL || provider->tag_len == 0U ||
        provider->tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_PARAM;
    }
    *tag_len = provider->tag_len;
    return XGL_OK;
}

/**
 * \brief           Serialize with one fresh security sequence per provider
 * invocation
 * \param[out]      buffer: Caller-owned output buffer
 * \param[in]       buffer_size: Available output capacity
 * \param[in]       frame: Borrowed frame data
 * \param[in,out]   security: Explicit trusted association state
 * \param[out]      bytes_written: Complete frame length on success
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_security_serialize_frame(uint8_t* buffer, size_t buffer_size,
                                         const xgl_frame_t* frame,
                                         xgl_security_ctx_t* security,
                                         size_t* bytes_written) {
    if (buffer == NULL || frame == NULL || security == NULL ||
        security->provider == NULL || bytes_written == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (security->busy) {
        return XGL_ERR_BUSY;
    }
    const xgl_auth_provider_t* provider = security->provider;
    *bytes_written = 0U;
    if (provider->tag_len == 0U || provider->tag_len > XGL_AUTH_TAG_MAX_LEN) {
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_frame_layout_t layout;
    xgl_error_t err = xgl_frame_encode_into(buffer, buffer_size, frame,
                                            provider->tag_len, &layout);
    if (err != XGL_OK) {
        return err;
    }

    size_t frame_len_without_crc = 0;
    err = xgl_security_sign_frame(
        security, buffer, buffer_size - XGL_CRC16_SIZE, layout.header_len,
        layout.payload_len, &frame_len_without_crc);
    if (err != XGL_OK) {
        return err;
    }
    if (frame_len_without_crc + XGL_CRC16_SIZE != layout.frame_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    return xgl_frame_finalize_crc(buffer, buffer_size, &layout, bytes_written);
}
