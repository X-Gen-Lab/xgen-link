/**
 * \file            interop_peer.c
 * \brief           Standalone host process for exercising Wire v1 exchanges.
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-11-26
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xgl/xgl.h>
#include <xgl/xgl_security.h>

#define INTEROP_CONNECTION 83U
#define INTEROP_EPOCH      29U
#define INTEROP_TYPE       37U

typedef struct {
    uint8_t incoming[4096];
    size_t pending;
    uint16_t local;
    uint16_t remote;
} interop_phy_t;

static void emit(const char* name, const uint8_t* bytes, size_t length) {
    printf("%s ", name);
    for (size_t i = 0U; i < length; ++i) {
        printf("%02x", (unsigned int)bytes[i]);
    }
    putchar('\n');
}

static xgl_error_t transmit(const uint8_t* bytes, size_t length,
                            void* context) {
    (void)context;
    emit("tx", bytes, length);
    return XGL_OK;
}

static xgl_error_t receive(uint8_t* bytes, size_t* length, void* context) {
    interop_phy_t* phy = context;
    if (*length > phy->pending) {
        *length = phy->pending;
    }
    memcpy(bytes, phy->incoming, *length);
    phy->pending -= *length;
    memmove(phy->incoming, phy->incoming + *length, phy->pending);
    return XGL_OK;
}

static xgl_error_t deliver(xgl_handle_t handle, const xgl_rx_info_t* info,
                           const uint8_t* bytes, size_t length, void* context) {
    const uint16_t source = info->source_id;
    const uint8_t type = info->data_type;
    const interop_phy_t* phy = context;
    (void)handle;
    emit(source == phy->remote && type == INTEROP_TYPE ? "rx" : "wrong", bytes,
         length);
    return XGL_OK;
}

#if XGL_FEATURE_AUTH
static uint32_t digest_bytes(uint32_t value, const uint8_t* bytes,
                             size_t length) {
    for (size_t i = 0U; i < length; ++i) {
        value = (value ^ bytes[i]) * UINT32_C(16777619);
    }
    return value;
}

/* A deterministic test provider makes AAD and nonce changes observable. */
static xgl_error_t sign_frame(const xgl_auth_input_t* input, uint8_t* tag,
                              size_t capacity, size_t* written, void* context) {
    (void)context;
    if (capacity < 4U) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    uint32_t value =
        digest_bytes(input->key_id, input->nonce, sizeof(input->nonce));
    value = digest_bytes(value, input->aad, input->aad_len);
    value = digest_bytes(value, input->payload, input->payload_len);
    for (unsigned i = 0U; i < 4U; ++i) {
        tag[i] = (uint8_t)(value >> (8U * i));
    }
    *written = 4U;
    return XGL_OK;
}

static xgl_error_t verify_frame(const xgl_auth_input_t* input,
                                const uint8_t* tag, size_t length, bool* valid,
                                void* context) {
    uint8_t expected[4];
    size_t written;
    const xgl_error_t error =
        sign_frame(input, expected, sizeof(expected), &written, context);
    *valid = error == XGL_OK && written == length &&
             memcmp(expected, tag, length) == 0;
    return error;
}
#endif

static int digit(char input) {
    if (input >= '0' && input <= '9') {
        return input - '0';
    }
    if (input >= 'a' && input <= 'f') {
        return input - 'a' + 10;
    }
    return -1;
}

static xgl_error_t enqueue(interop_phy_t* phy, const char* text) {
    size_t length = strcspn(text, "\r\n");
    if (length % 2U != 0U ||
        length / 2U > sizeof(phy->incoming) - phy->pending) {
        return XGL_ERR_INVALID_PARAM;
    }
    for (size_t i = 0U; i < length; i += 2U) {
        int high = digit(text[i]);
        int low = digit(text[i + 1U]);
        if (high < 0 || low < 0) {
            return XGL_ERR_INVALID_PARAM;
        }
        phy->incoming[phy->pending + i / 2U] = (uint8_t)(16 * high + low);
    }
    phy->pending += length / 2U;
    return XGL_OK;
}

static xgl_error_t execute(xgl_handle_t handle, interop_phy_t* phy,
                           const char* command) {
    if (strncmp(command, "put ", 4U) == 0) {
        return enqueue(phy, command + 4U);
    }
    unsigned now;
    if (sscanf(command, "step %u", &now) == 1) {
        const xgl_work_budget_t budget = {128U, 1000U};
        return xgl_step(handle, (uint32_t)now, &budget);
    }
    size_t length;
    uint8_t payload[512];
    const bool expect_failed_scope = strncmp(command, "reject ", 7U) == 0;
    if (expect_failed_scope) {
        command += 7U;
    }
    if (sscanf(command, "send %zu %u", &length, &now) != 2 || length == 0U ||
        length > sizeof(payload)) {
        return XGL_ERR_INVALID_PARAM;
    }
    for (size_t i = 0U; i < length; ++i) {
        payload[i] = (uint8_t)(i ^ phy->local ^ 0xa5U);
    }
    const xgl_tx_data_t request = {
        .target_id = phy->remote,
        .data_type = INTEROP_TYPE,
        .data = payload,
        .data_len = length,
        .reliable = true,
        .connection_id = INTEROP_CONNECTION,
        .session_epoch = INTEROP_EPOCH,
    };
    const xgl_error_t error = xgl_send_at(handle, &request, (uint32_t)now);
    if (expect_failed_scope) {
        return error == XGL_ERR_ACK_TIMEOUT ? XGL_OK : XGL_ERR_INVALID_FRAME;
    }
    return error;
}

int main(int argc, char** argv) {
    if (argc != 3 || (strcmp(argv[1], "1") != 0 && strcmp(argv[1], "2") != 0)) {
        return EXIT_FAILURE;
    }
    interop_phy_t state = {0};
    state.local = (uint16_t)(argv[1][0] - '0');
    state.remote = (uint16_t)(3U - state.local);
    xgl_phy_ops_t phy = {transmit, receive, &state};
    xgl_route_item_t route = {state.remote, &phy, 128U, 1000U};
    const bool full = strcmp(argv[2], "full") == 0;
    if (full ? !(XGL_FEATURE_AUTH && XGL_FEATURE_OUT_OF_ORDER)
             : strcmp(argv[2], "boot") != 0) {
        return EXIT_FAILURE;
    }
    xgl_config_t config = {
        .source_id = state.local,
        .memory = {.rx_buffer_size = 128U},
        .protocol = {.ack_timeout_ms = 1000U,
                     .max_retry_count = full ? 5U : 3U,
                     .window_size = full ? 4U : 1U,
                     .max_frame_size = 128U},
        .features = {.max_peers = full ? 2U : 1U,
                     .max_tx_packets = full ? 4U : 1U,
                     .max_rx_buffered_packets = full ? 4U : 0U,
                     .peer_idle_timeout_ms = 60000U},
    };
    config.route_table = &route;
    config.route_table_len = 1U;
    config.rx_callback = deliver;
    config.rx_user_data = &state;
#if XGL_FEATURE_AUTH
    xgl_auth_provider_t provider = {sign_frame, verify_frame, 4U, NULL};
    config.auth_required = full;
    config.auth_provider = config.auth_required ? &provider : NULL;
#endif

    static xgl_max_align_t storage[4096];
    xgl_handle_t handle = NULL;
    if (xgl_init_static(&config, storage, sizeof(storage), &handle) != XGL_OK) {
        return EXIT_FAILURE;
    }
#if XGL_FEATURE_AUTH
    if (config.auth_required) {
        const xgl_security_session_config_t session = {
            .remote_id = state.remote,
            .connection_id = INTEROP_CONNECTION,
            .session_epoch = INTEROP_EPOCH,
            .tx_key_id = 7U,
            .rx_key_id = 7U,
            .tx_nonce_prefix = state.local,
            .rx_nonce_prefix = state.remote,
        };
        if (xgl_install_security_session(handle, &session) != XGL_OK) {
            xgl_destroy(handle);
            return EXIT_FAILURE;
        }
    }
#endif

    char command[1024];
    while (fgets(command, sizeof(command), stdin) != NULL) {
        printf("end %u\n", (unsigned int)execute(handle, &state, command));
        fflush(stdout);
    }
    xgl_destroy(handle);
    return EXIT_SUCCESS;
}
