/**
 * \file            xgl_security.c
 * \brief           Security helpers for authenticated production transport
 */

#include <xgl/internal/xgl_security.h>

#include <string.h>
#include <xgen/bytes/bytes.h>

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Initialize a bounded replay window for one trusted scope
 * \param[out]      window: Replay state
 * \param[in]       source_id: Remote endpoint identifier
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Trusted epoch
 * \param[in]       window_size: Accepted reordering distance, from 1 to 64
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_replay_window_init(xgl_replay_window_t* window,
                                   uint16_t source_id, uint32_t connection_id,
                                   uint32_t session_epoch,
                                   uint8_t window_size) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (window == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (source_id == 0U || window_size == 0U || window_size > 64U) {
        return XGL_ERR_INVALID_PARAM;
    }

    memset(window, 0, sizeof(*window));
    window->source_id = source_id;
    window->connection_id = connection_id;
    window->session_epoch = session_epoch;
    window->window_size = window_size;

    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Classify and commit a security sequence in one replay window
 * \param[in,out]   window: Replay state or a tentative copy before verification
 * \param[in]       source_id: Remote endpoint identifier
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Trusted epoch
 * \param[in]       packet_number: Independent 64-bit security sequence
 * \return          New sequence, duplicate, or rejected scope/window position
 */
xgl_replay_result_t xgl_replay_window_check(xgl_replay_window_t* window,
                                            uint16_t source_id,
                                            uint32_t connection_id,
                                            uint32_t session_epoch,
                                            uint64_t packet_number) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    if (window == NULL) {
        return XGL_REPLAY_REJECT;
    }

    if (source_id != window->source_id ||
        connection_id != window->connection_id ||
        session_epoch != window->session_epoch) {
        return XGL_REPLAY_REJECT;
    }

    if (!window->has_largest) {
        window->largest_packet_number = packet_number;
        window->received_bitmap = 1U;
        window->has_largest = true;
        return XGL_REPLAY_ACCEPT_NEW;
    }

    if (packet_number > window->largest_packet_number) {
        uint64_t diff = packet_number - window->largest_packet_number;
        if (diff >= 64U) {
            window->received_bitmap = 1U;
        } else {
            window->received_bitmap <<= diff;
            window->received_bitmap |= 1U;
        }
        window->largest_packet_number = packet_number;
        return XGL_REPLAY_ACCEPT_NEW;
    }

    uint64_t offset = window->largest_packet_number - packet_number;
    if (offset >= window->window_size || offset >= 64U) {
        return XGL_REPLAY_REJECT;
    }

    uint64_t bit = 1ULL << offset;
    if ((window->received_bitmap & bit) != 0U) {
        return XGL_REPLAY_ACCEPT_DUPLICATE;
    }

    window->received_bitmap |= bit;
    return XGL_REPLAY_ACCEPT_NEW;
}

/**
 * \brief           Commit a previously unseen security sequence
 * \param[in,out]   window: Replay state
 * \param[in]       source_id: Remote endpoint identifier
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Trusted epoch
 * \param[in]       packet_number: Independent 64-bit security sequence
 * \return          True only for a previously unseen in-window sequence
 */
bool xgl_replay_window_accept(xgl_replay_window_t* window, uint16_t source_id,
                              uint32_t connection_id, uint32_t session_epoch,
                              uint64_t packet_number) {
    return xgl_replay_window_check(window, source_id, connection_id,
                                   session_epoch,
                                   packet_number) == XGL_REPLAY_ACCEPT_NEW;
}

#if XGL_FEATURE_AUTH
/**
 * \brief           Find an explicitly installed association, including
 * tombstones
 * \param[in,out]   ctx: Security context
 * \param[in]       remote_id: Remote endpoint
 * \param[in]       connection_id: Connection identity
 * \param[in]       epoch: Trusted epoch
 * \return          Matching slot or NULL
 */
static xgl_security_session_t* security_find(xgl_security_ctx_t* ctx,
                                             uint16_t remote_id,
                                             uint32_t connection_id,
                                             uint32_t epoch) {
    for (size_t i = 0; i < XGL_SECURITY_SESSION_CAPACITY; ++i) {
        xgl_security_session_t* session = &ctx->sessions[i];
        if (session->used && session->config.remote_id == remote_id &&
            session->config.connection_id == connection_id &&
            session->config.session_epoch == epoch) {
            return session;
        }
    }
    return NULL;
}

/**
 * \brief           Initialize an empty security context
 * \param[out]      ctx: Security context
 * \param[in]       local_id: Local endpoint identifier
 * \param[in]       auth_required: Require authentication on local delivery
 * \param[in]       provider: Borrowed provider
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_security_init(xgl_security_ctx_t* ctx, uint16_t local_id,
                              bool auth_required,
                              const xgl_auth_provider_t* provider) {
    if (ctx == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if ((auth_required && provider == NULL) ||
        (provider != NULL &&
         (provider->sign == NULL || provider->verify == NULL ||
          provider->tag_len == 0U ||
          provider->tag_len > XGL_AUTH_TAG_MAX_LEN))) {
        return XGL_ERR_INVALID_PARAM;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->local_id = local_id;
    ctx->auth_required = auth_required;
    ctx->provider = provider;
    return XGL_OK;
}

/**
 * \brief           Check whether two associations reuse a key and nonce prefix
 * \param[in]       a: Existing association
 * \param[in]       b: Proposed association
 * \return          True if any directional nonce domain overlaps
 */
static bool security_domain_overlap(const xgl_security_session_config_t* a,
                                    const xgl_security_session_config_t* b) {
    return (a->tx_key_id == b->tx_key_id &&
            a->tx_nonce_prefix == b->tx_nonce_prefix) ||
           (a->tx_key_id == b->rx_key_id &&
            a->tx_nonce_prefix == b->rx_nonce_prefix) ||
           (a->rx_key_id == b->tx_key_id &&
            a->rx_nonce_prefix == b->tx_nonce_prefix) ||
           (a->rx_key_id == b->rx_key_id &&
            a->rx_nonce_prefix == b->rx_nonce_prefix);
}

/**
 * \brief           Install trusted state without replacing old nonce domains
 * \param[in,out]   ctx: Security context
 * \param[in]       config: Association copied into context
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t
xgl_security_session_install(xgl_security_ctx_t* ctx,
                             const xgl_security_session_config_t* config) {
    if (ctx == NULL || config == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (ctx->busy) {
        return XGL_ERR_BUSY;
    }
    if (ctx->provider == NULL || ctx->local_id == 0U ||
        config->remote_id == 0U || config->remote_id == ctx->local_id ||
        (config->tx_key_id == config->rx_key_id &&
         config->tx_nonce_prefix == config->rx_nonce_prefix)) {
        return XGL_ERR_INVALID_PARAM;
    }
    xgl_security_session_t* free_slot = NULL;
    for (size_t i = 0; i < XGL_SECURITY_SESSION_CAPACITY; ++i) {
        xgl_security_session_t* slot = &ctx->sessions[i];
        if (!slot->used) {
            if (free_slot == NULL) {
                free_slot = slot;
            }
            continue;
        }
        if ((slot->config.remote_id == config->remote_id &&
             slot->config.connection_id == config->connection_id &&
             slot->config.session_epoch == config->session_epoch) ||
            security_domain_overlap(&slot->config, config)) {
            return XGL_ERR_INVALID_PARAM;
        }
    }
    if (free_slot == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->config = *config;
    free_slot->tx_next_seq = config->tx_initial_seq;
    (void)xgl_replay_window_init(&free_slot->replay, config->remote_id,
                                 config->connection_id, config->session_epoch,
                                 64U);
    free_slot->used = true;
    free_slot->active = true;
    return XGL_OK;
}

/**
 * \brief           Close trusted state while retaining its nonce-domain
 * reservation
 * \param[in,out]   ctx: Security context
 * \param[in]       remote_id: Remote endpoint
 * \param[in]       connection_id: Connection identity
 * \param[in]       session_epoch: Trusted epoch
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_security_session_close(xgl_security_ctx_t* ctx,
                                       uint16_t remote_id,
                                       uint32_t connection_id,
                                       uint32_t session_epoch) {
    if (ctx == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (ctx->busy) {
        return XGL_ERR_BUSY;
    }
    xgl_security_session_t* session =
        security_find(ctx, remote_id, connection_id, session_epoch);
    if (session == NULL || !session->active) {
        return XGL_ERR_NOT_FOUND;
    }
    session->active = false;
    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Construct strict nonce and canonical AAD for a provider call
 * \param[in,out]   ctx: Context owning canonical AAD scratch
 * \param[out]      input: Versioned borrowed provider input
 * \param[in]       header: Parsed wire header
 * \param[in]       buffer: Actual frame bytes
 * \param[in]       epoch: Trusted epoch
 * \param[in]       key_id: Directional key identifier
 * \param[in]       prefix: Directional nonce prefix
 * \param[in]       sequence: Independent security sequence
 */
static void security_prepare_input(xgl_security_ctx_t* ctx,
                                   xgl_auth_input_t* input,
                                   const xgl_wire_header_t* header,
                                   const uint8_t* buffer, uint32_t epoch,
                                   uint32_t key_id, uint32_t prefix,
                                   uint64_t sequence) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    memset(input, 0, sizeof(*input));
    input->version = XGL_AUTH_INPUT_VERSION;
    input->wire_version = header->version;
    input->source_id = header->source_id;
    input->target_id = header->target_id;
    input->connection_id = header->connection_id;
    input->session_epoch = epoch;
    input->key_id = key_id;
    input->security_seq = sequence;
    xgb_serialize_u32_be(input->nonce, prefix);
    xgb_serialize_u64_be(input->nonce + 4U, sequence);
    memcpy(ctx->aad_scratch, buffer, header->header_len);
    ctx->aad_scratch[6] = 0U;
    ctx->aad_scratch[22] = 0U;
    ctx->aad_scratch[23] = 0U;
    input->aad = ctx->aad_scratch;
    input->aad_len = header->header_len;
    input->payload = buffer + header->header_len;
    input->payload_len = header->payload_len;
}

/**
 * \brief           Consume a unique sequence and authenticate prepared frame
 * bytes
 * \param[in,out]   ctx: Security context
 * \param[in,out]   buffer: Encoded header, extensions and payload
 * \param[in]       capacity: Capacity excluding final CRC
 * \param[in]       aad_len: Encoded header length
 * \param[in]       payload_len: Encoded payload length
 * \param[out]      frame_len: Produced length excluding final CRC
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_security_sign_frame(xgl_security_ctx_t* ctx, uint8_t* buffer,
                                    size_t capacity, size_t aad_len,
                                    size_t payload_len, size_t* frame_len) {
    if (ctx == NULL || buffer == NULL || frame_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    *frame_len = 0U;
    if (ctx->busy) {
        return XGL_ERR_BUSY;
    }
    if (ctx->provider == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (aad_len > capacity || payload_len > capacity - aad_len ||
        ctx->provider->tag_len > capacity - aad_len - payload_len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    xgl_wire_header_t header;
    xgl_error_t err = xgl_wire_decode_header(&header, buffer, aad_len);
    if (err != XGL_OK) {
        return err;
    }
    if (header.header_len != aad_len || header.payload_len != payload_len ||
        header.source_id != ctx->local_id ||
        (header.flags & XGL_WIRE_FLAG_AUTHENTICATED) == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }
    xgl_wire_ext_metadata_t metadata;
    err = xgl_wire_decode_ext_metadata(buffer + XGL_WIRE_BASE_HEADER_SIZE,
                                       aad_len - XGL_WIRE_BASE_HEADER_SIZE,
                                       &metadata);
    if (err != XGL_OK || !metadata.has_security_ext ||
        metadata.auth_tag_len != ctx->provider->tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }
    xgl_security_session_t* session = security_find(
        ctx, header.target_id, header.connection_id, metadata.session_epoch);
    if (session == NULL || !session->active) {
        return XGL_ERR_NOT_FOUND;
    }
    if (session->exhausted) {
        return XGL_ERR_SEQUENCE_ERROR;
    }
    xgl_wire_ext_cursor_t cursor;
    xgl_wire_ext_t ext;
    (void)xgl_wire_ext_cursor_init(&cursor, buffer + XGL_WIRE_BASE_HEADER_SIZE,
                                   aad_len - XGL_WIRE_BASE_HEADER_SIZE);
    while (xgl_wire_ext_cursor_next(&cursor, &ext) == XGL_OK) {
        if (ext.type == XGL_WIRE_EXT_SECURITY) {
            break;
        }
    }
    uint64_t sequence = session->tx_next_seq;
    if (sequence == UINT64_MAX) {
        session->exhausted = true;
    } else {
        session->tx_next_seq++;
    }
    size_t written = 0U;
    err = xgl_wire_encode_security_ext_value(
        buffer + (ext.value - buffer), ext.len, session->config.tx_key_id,
        sequence, metadata.auth_tag_len, &written);
    if (err != XGL_OK) {
        return err;
    }
    ctx->busy = true;
    xgl_auth_input_t input;
    security_prepare_input(ctx, &input, &header, buffer, metadata.session_epoch,
                           session->config.tx_key_id,
                           session->config.tx_nonce_prefix, sequence);
    size_t tag_len = 0U;
    size_t tag_offset = aad_len + payload_len;
    err =
        ctx->provider->sign(&input, buffer + tag_offset, ctx->provider->tag_len,
                            &tag_len, ctx->provider->user_data);
    ctx->busy = false;
    if (err != XGL_OK) {
        return err;
    }
    if (tag_len != ctx->provider->tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }
    *frame_len = tag_offset + tag_len;
    return XGL_OK;
}

/**
 * \brief           Verify local authentication before committing replay state
 * \param[in,out]   ctx: Security context
 * \param[in]       view: Wire-validated immutable frame view
 * \return          XGL_OK on acceptance, error code otherwise
 */
xgl_error_t xgl_security_verify_frame(xgl_security_ctx_t* ctx,
                                      const xgl_wire_frame_view_t* view) {
    if (ctx == NULL || view == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (ctx->busy) {
        return XGL_ERR_BUSY;
    }
    if (!view->authenticated) {
        return ctx->auth_required ? XGL_ERR_INVALID_FRAME : XGL_OK;
    }
    if (view->header.target_id != ctx->local_id) {
        return XGL_ERR_INVALID_FRAME;
    }
    if (ctx->provider == NULL || !view->has_security_ext ||
        view->auth_tag_len != ctx->provider->tag_len) {
        return XGL_ERR_INVALID_FRAME;
    }
    xgl_security_session_t* session =
        security_find(ctx, view->header.source_id, view->header.connection_id,
                      view->session_epoch);
    if (session == NULL || !session->active ||
        view->auth_key_id != session->config.rx_key_id ||
        view->nonce_id < session->config.rx_min_seq) {
        return XGL_ERR_INVALID_FRAME;
    }
    xgl_replay_window_t candidate = session->replay;
    if (xgl_replay_window_check(&candidate, view->header.source_id,
                                view->header.connection_id, view->session_epoch,
                                view->nonce_id) != XGL_REPLAY_ACCEPT_NEW) {
        return XGL_ERR_INVALID_FRAME;
    }
    ctx->busy = true;
    xgl_auth_input_t input;
    security_prepare_input(ctx, &input, &view->header, view->frame_buf,
                           view->session_epoch, session->config.rx_key_id,
                           session->config.rx_nonce_prefix, view->nonce_id);
    bool valid = false;
    xgl_error_t err = ctx->provider->verify(
        &input, view->payload + view->payload_len, view->auth_tag_len, &valid,
        ctx->provider->user_data);
    ctx->busy = false;
    if (err != XGL_OK || !valid) {
        return XGL_ERR_INVALID_FRAME;
    }
    session->replay = candidate;
    return XGL_OK;
}
#endif
