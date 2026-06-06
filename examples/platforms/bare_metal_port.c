/**
 * \file            bare_metal_port.c
 * \brief           Bare-metal xgen-link porting skeleton
 */

#include "xgl/xgl.h"

static xgl_error_t board_uart_write(const uint8_t* data, size_t len) {
    (void)data;
    (void)len;
    return XGL_OK;
}

static size_t board_uart_read(uint8_t* data, size_t max_len) {
    (void)data;
    (void)max_len;
    return 0;
}

static xgl_error_t bare_metal_tx(const uint8_t* data, size_t len, void* user_data) {
    (void)user_data;
    return board_uart_write(data, len);
}

static xgl_error_t bare_metal_rx(uint8_t* buffer, size_t* len, void* user_data) {
    (void)user_data;
    if (buffer == NULL || len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *len = board_uart_read(buffer, *len);
    return XGL_OK;
}

void xgl_bare_metal_poll_example(void) {
    xgl_phy_ops_t phy = {
        .tx = bare_metal_tx,
        .rx = bare_metal_rx,
        .user_data = NULL
    };
    xgl_route_item_t routes[] = {
        { .target_id = 2, .phy = &phy, .max_frame_size = 128, .read_freq_hz = 100, .metric = 1 }
    };

    xgl_config_t config;
    xgl_config_get_preset_tiny(&config);
    config.source_id = 1;
    config.route_table = routes;
    config.route_table_len = 1;

    xgl_handle_t handle = xgl_create(&config);
    if (handle == NULL || xgl_init(handle) != XGL_OK) {
        xgl_destroy(handle);
        return;
    }

    for (;;) {
        xgl_run(handle, 100);
    }
}
