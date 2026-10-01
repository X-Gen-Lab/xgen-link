/**
 * \file            consumer.c
 * \brief           Link the actual static Boot API paths into a Cortex-M0 image
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>

#include "footprint_config.h"
#include "workspace_layout.h"

/** \brief           Observations available to a debugger if ported to hardware
 */
typedef struct {
    xgl_error_t initialization;
    xgl_error_t send;
    xgl_error_t step;
    size_t workspace_required;
    uint32_t transmitted_frames;
    uint32_t transmitted_checksum;
    uint32_t accepted_messages;
    uint32_t next_timeout_ms;
} footprint_observations_t;

volatile footprint_observations_t xgl_boot_observations;
_Alignas(
    xgm_max_align_t) static uint8_t workspace[XGL_FOOTPRINT_WORKSPACE_SIZE];

/** \brief           Exercise serialization without claiming a physical driver
 */
static xgl_error_t probe_tx(const uint8_t* data, size_t length,
                            void* user_data) {
    (void)user_data;
    for (size_t i = 0U; i < length; ++i) {
        xgl_boot_observations.transmitted_checksum += data[i];
    }
    ++xgl_boot_observations.transmitted_frames;
    return XGL_OK;
}

/** \brief           Supply an empty nonblocking receive source */
static xgl_error_t probe_rx(uint8_t* data, size_t* length, void* user_data) {
    (void)data;
    (void)user_data;
    *length = 0U;
    return XGL_OK;
}

/** \brief           Retain the application acceptance callback in the image */
static xgl_error_t probe_accept(xgl_handle_t handle, uint16_t source_id,
                                uint8_t data_type, const uint8_t* data,
                                size_t length, void* user_data) {
    (void)handle;
    (void)source_id;
    (void)data_type;
    (void)data;
    (void)length;
    (void)user_data;
    ++xgl_boot_observations.accepted_messages;
    return XGL_OK;
}

/** \brief           Exercise initialization, reliable TX, timers, and cleanup
 */
static void probe_main(void) {
    static xgl_phy_ops_t phy = {probe_tx, probe_rx, NULL};
    static xgl_route_item_t route = {2U, &phy, XGL_FOOTPRINT_FRAME_SIZE, 100U,
                                     1U};
    /* The immutable configuration is borrowed until destroy and can live in
     * Flash. Every enabled capacity is explicit; disabled classes remain zero.
     */
    static const xgl_config_t config = {
        .name = "boot-footprint",
        .source_id = 1U,
        .memory = {.rx_buffer_size = XGL_FOOTPRINT_RX_SIZE, .allocator = NULL},
        .protocol = {.ack_timeout_ms = 1000U,
                     .max_retry_count = 3U,
                     .window_size = XGL_FOOTPRINT_WINDOW,
                     .max_frame_size = XGL_FOOTPRINT_FRAME_SIZE},
        .features = {.max_tx_packets = XGL_FOOTPRINT_TX_PACKETS,
                     .max_rx_buffered_packets = 0U,
                     .max_reassembly_bytes = 0U,
                     .max_tx_message_bytes = 0U,
                     .peer_idle_timeout_ms = 60000U,
                     .max_peers = XGL_FOOTPRINT_PEERS},
        .route_table = &route,
        .route_table_len = XGL_FOOTPRINT_ROUTES,
        .rx_accept_callback = probe_accept};
    xgl_memory_requirements_t requirements;
    xgl_error_t error = xgl_memory_requirements(&config, &requirements);
    xgl_boot_observations.initialization = error;
    if (error != XGL_OK) {
        return;
    }
    xgl_boot_observations.workspace_required = requirements.size;
    if (requirements.size != sizeof(workspace)) {
        xgl_boot_observations.initialization = XGL_ERR_BUFFER_TOO_SMALL;
        return;
    }
    xgl_handle_t handle = NULL;
    error = xgl_init_static(&config, workspace, sizeof(workspace), &handle);
    xgl_boot_observations.initialization = error;
    if (error != XGL_OK) {
        return;
    }
    static const uint8_t payload[] = {0x12U, 0x34U, 0x56U, 0x78U};
    xgl_tx_data_t tx = {0};
    tx.target_id = 2U;
    tx.data = payload;
    tx.data_len = sizeof(payload);
    tx.reliable = true;
    xgl_boot_observations.send = xgl_send_at(handle, &tx, 0U);
    const xgl_work_budget_t budget = {XGL_FOOTPRINT_FRAME_SIZE, 0U};
    xgl_boot_observations.step = xgl_step(handle, 2000U, &budget);
    uint32_t delay = 0U;
    if (xgl_next_timeout(handle, 2000U, &delay)) {
        xgl_boot_observations.next_timeout_ms = delay;
    }
    xgl_destroy(handle);
}

extern uint8_t __data_load;
extern uint8_t __data_start;
extern uint8_t __data_end;
extern uint8_t __bss_start;
extern uint8_t __bss_end;

/** \brief           Minimal generic C startup for resource measurement only */
void Reset_Handler(void) {
    const uint8_t* source = &__data_load;
    for (uint8_t* destination = &__data_start; destination < &__data_end;) {
        *destination++ = *source++;
    }
    for (uint8_t* destination = &__bss_start; destination < &__bss_end;) {
        *destination++ = 0U;
    }
    probe_main();
    for (;;) {
        /* No peripheral setup or watchdog policy is supplied by this probe. */
    }
}
