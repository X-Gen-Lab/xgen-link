/**
 * \file            main.c
 * \brief           Three-node forwarding across two independent PHY parsers
 * \author          X-Gen Lab
 */
#include <stdio.h>

#include "../common/sim_link.h"

/**
 * \brief           Count one valid application payload without calling protocol
 * APIs
 * \param[in]       handle: Callback instance, unused
 * \param[in]       source: End-to-end sender
 * \param[in]       type: Application type
 * \param[in]       data: Borrowed payload
 * \param[in]       length: Payload length
 * \param[in,out]   context: Delivery counter
 */
static void receive_payload(xgl_handle_t handle, uint16_t source, uint8_t type,
                            const uint8_t* data, size_t length, void* context) {
    (void)handle;
    (void)type;
    if (source == 1U && length == 3U && memcmp(data, "hop", 3U) == 0) {
        (*(unsigned*)context)++;
    }
}

/** \brief           Forward one reliable message and its ACK through a router.
 */
int main(void) {
    static sim_node_t a, router, b;
    static sim_channel_t ar, ra, rb, br;
    sim_port_t pa = {&ra, &ar, false, 0};
    sim_port_t pr1 = {&ar, &ra, false, 0};
    sim_port_t pr2 = {&br, &rb, false, 0};
    sim_port_t pb = {&rb, &br, false, 0};
    xgl_phy_ops_t phys[4] = {{sim_tx, sim_rx, &pa},
                             {sim_tx, sim_rx, &pr1},
                             {sim_tx, sim_rx, &pr2},
                             {sim_tx, sim_rx, &pb}};
    unsigned delivered = 0;
    sim_prepare(&a, 1U);
    sim_prepare(&router, 2U);
    sim_prepare(&b, 3U);
    sim_route(&a, 0U, 3U, &phys[0]);
    sim_route(&router, 0U, 1U, &phys[1]);
    sim_route(&router, 1U, 3U, &phys[2]);
    sim_route(&b, 0U, 1U, &phys[3]);
    b.config.rx_callback = receive_payload;
    b.config.callback_user_data = &delivered;
    if (sim_init(&a) != XGL_OK || sim_init(&router) != XGL_OK ||
        sim_init(&b) != XGL_OK) {
        return 1;
    }
    const xgl_tx_data_t tx = {.target_id = 3U,
                              .data_type = 3U,
                              .data = (const uint8_t*)"hop",
                              .data_len = 3U,
                              .reliable = true};
    if (xgl_send_at(a.handle, &tx, 0U) != XGL_OK) {
        return 2;
    }
    for (uint32_t now = 0; now < 200U; ++now) {
        if (sim_step(&a, now) != XGL_OK || sim_step(&router, now) != XGL_OK ||
            sim_step(&b, now) != XGL_OK) {
            return 3;
        }
    }
    xgl_destroy(a.handle);
    xgl_destroy(router.handle);
    xgl_destroy(b.handle);
    printf("Two-link forwarding: %s\n", delivered == 1U ? "PASS" : "FAIL");
    return delivered == 1U ? 0 : 4;
}
