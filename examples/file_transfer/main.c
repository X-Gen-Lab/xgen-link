/**
 * \file            main.c
 * \brief           Reliable fixed-block transfer using bounded host storage
 * \author          X-Gen Lab
 */
#include <stdio.h>
#include <xgen/crc/crc.h>

#include "../common/sim_link.h"
#define FILE_BYTES  1024U
#define BLOCK_BYTES 64U

typedef struct {
    uint8_t bytes[FILE_BYTES];
    size_t used;
} file_receiver_t;

/**
 * \brief           Accept each ordered block into bounded application storage
 * \param[in]       handle: Callback instance, unused
 * \param[in]       source: Sender identity
 * \param[in]       type: Application type
 * \param[in]       data: Borrowed block
 * \param[in]       length: Block length
 * \param[in,out]   context: Receiver storage
 * \return          XGL_OK when copied, INVALID_PARAM on overflow
 */
static xgl_error_t file_accept(xgl_handle_t handle, uint16_t source,
                               uint8_t type, const uint8_t* data, size_t length,
                               void* context) {
    (void)handle;
    (void)source;
    (void)type;
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
    static sim_channel_t to_sender, to_receiver;
    static file_receiver_t application;
    uint8_t source[FILE_BYTES];
    for (size_t i = 0; i < sizeof(source); ++i) {
        source[i] = (uint8_t)i;
    }
    sim_port_t sender_port = {&to_sender, &to_receiver, false, 0};
    sim_port_t receiver_port = {&to_receiver, &to_sender, false, 0};
    xgl_phy_ops_t sender_phy = {sim_tx, sim_rx, &sender_port};
    xgl_phy_ops_t receiver_phy = {sim_tx, sim_rx, &receiver_port};
    sim_prepare(&sender, 1U);
    sim_prepare(&receiver, 2U);
    sim_route(&sender, 0U, 2U, &sender_phy);
    sim_route(&receiver, 0U, 1U, &receiver_phy);
    receiver.config.rx_accept_callback = file_accept;
    receiver.config.callback_user_data = &application;
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
    printf("Fixed-block transfer: %s, CRC16=%04X (not authentication)\n",
           complete ? "PASS" : "FAIL",
           (unsigned)xgcrc_crc16_modbus(application.bytes, application.used));
    xgl_destroy(sender.handle);
    xgl_destroy(receiver.handle);
    return complete ? 0 : 4;
}
