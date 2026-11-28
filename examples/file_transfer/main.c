/**
 * \file            main.c
 * \brief           Reliable fixed-block transfer using bounded host storage
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-02-28
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */
#include <stdio.h>

#include "../common/sim_link.h"

#define FILE_BYTES  1024U
#define BLOCK_BYTES (SIM_TYPED_PAYLOAD_CAPACITY >= 64U ? 64U : 32U)

_Static_assert(BLOCK_BYTES <= SIM_TYPED_PAYLOAD_CAPACITY &&
                   FILE_BYTES % BLOCK_BYTES == 0U,
               "The file must fit in whole single-frame blocks");

typedef struct {
    uint8_t bytes[FILE_BYTES];
    size_t used;
} file_receiver_t;

/**
 * \brief           Accept each ordered block into bounded application storage
 * \details         Accept after copying the borrowed block. Reject a block
 *                  that exceeds application capacity.
 */
static xgl_error_t file_accept(xgl_handle_t handle, const xgl_rx_info_t* info,
                               const uint8_t* data, size_t length,
                               void* context) {
    (void)handle;
    (void)info;
    file_receiver_t* receiver = context;
    if (length > sizeof(receiver->bytes) - receiver->used) {
        return XGL_ERR_INVALID_PARAM;
    }
    memcpy(receiver->bytes + receiver->used, data, length);
    receiver->used += length;
    return XGL_OK;
}

/** \brief           Transfer fixed blocks, waiting for window availability
 * between sends. */
int main(void) {
    static sim_node_t sender, receiver;
    static sim_link_t link;
    static file_receiver_t application;
    uint8_t source[FILE_BYTES];
    for (size_t i = 0; i < sizeof(source); ++i) {
        source[i] = (uint8_t)i;
    }
    sim_connect(&link);
    sim_prepare(&sender, 1U);
    sim_prepare(&receiver, 2U);
    sim_route(&sender, 0U, 2U, &link.phys[0]);
    sim_route(&receiver, 0U, 1U, &link.phys[1]);
    receiver.config.rx_callback = file_accept;
    receiver.config.rx_user_data = &application;
    if (sim_init(&sender) != XGL_OK || sim_init(&receiver) != XGL_OK) {
        return 1;
    }
    size_t submitted = 0U;
    for (uint32_t now = 0; now < 5000U && application.used < FILE_BYTES;
         ++now) {
        if (submitted < FILE_BYTES) {
            xgl_tx_data_t block = {.target_id = 2U,
                                   .data_type = 2U,
                                   .data = source + submitted,
                                   .data_len = BLOCK_BYTES,
                                   .reliable = true};
            xgl_error_t error = xgl_send_at(sender.handle, &block, now);
            if (error == XGL_OK) {
                submitted += BLOCK_BYTES;
            } else if (error != XGL_ERR_WINDOW_FULL && error != XGL_ERR_BUSY) {
                return 2;
            }
        }
        if (sim_step(&sender, now) != XGL_OK ||
            sim_step(&receiver, now) != XGL_OK) {
            return 3;
        }
    }
    bool complete = application.used == sizeof(source) &&
                    memcmp(source, application.bytes, sizeof(source)) == 0;
    printf("Fixed-block transfer: %s (%zu bytes received)\n",
           complete ? "PASS" : "FAIL", application.used);
    xgl_destroy(sender.handle);
    xgl_destroy(receiver.handle);
    return complete ? 0 : 4;
}
