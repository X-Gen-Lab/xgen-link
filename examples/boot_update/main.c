/**
 * \file            main.c
 * \brief           Static fixed-block update with slow Flash admission and lost
 * ACK recovery
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-10-01
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */
#include <stdio.h>
#include <xgl/xgl_diagnostics.h>

#include "../common/sim_link.h"

#define IMAGE_BYTES          512U
#define FLASH_OFFSET_BYTES   4U
#define FLASH_BLOCK_CAPACITY (SIM_TYPED_PAYLOAD_CAPACITY - FLASH_OFFSET_BYTES)
#define FLASH_BLOCK_BYTES                                                      \
    (FLASH_BLOCK_CAPACITY >= 64U   ? 64U                                       \
     : FLASH_BLOCK_CAPACITY >= 32U ? 32U                                       \
                                   : 16U)
#define BLOCK_TYPE  7U
#define COMMIT_TYPE 8U

_Static_assert(FLASH_OFFSET_BYTES + FLASH_BLOCK_BYTES <=
                       SIM_TYPED_PAYLOAD_CAPACITY &&
                   IMAGE_BYTES % FLASH_BLOCK_BYTES == 0U,
               "The image must fit in whole single-frame blocks");

typedef struct {
    uint8_t flash[IMAGE_BYTES];
    uint8_t staged[FLASH_BLOCK_BYTES];
    size_t committed;
    uint32_t now;
    uint32_t ready_at;
    bool staging;
    bool notice_pending;
    bool dropped_ack;
    unsigned busy_returns;
    unsigned flash_writes;
    unsigned errors;
    sim_port_t* port;
} flash_app_t;

typedef struct {
    size_t confirmed;
    unsigned errors;
} host_app_t;

/* Offset and commit messages use the update application's own byte format. */
static uint32_t update_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void update_write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

/* Compare the images independently of the protocol frame checks. */
static uint16_t image_crc16(const uint8_t* image, size_t length) {
    uint16_t crc = UINT16_MAX;
    for (size_t offset = 0; offset < length; ++offset) {
        crc = (uint16_t)(crc ^ image[offset]);
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const uint16_t polynomial = (crc & 1U) != 0U ? 0xA001U : 0U;
            crc = (uint16_t)((crc >> 1U) ^ polynomial);
        }
    }
    return crc;
}

/**
 * \brief           Admit one copied block; Flash writes happen outside this
 * callback
 * \details         Each message carries a little-endian offset and one fixed-
 *                  size block. Return BUSY while the staging slot is occupied
 *                  and accept only after copying the block.
 */
static xgl_error_t flash_accept(xgl_handle_t handle, const xgl_rx_info_t* info,
                                const uint8_t* data, size_t length,
                                void* context) {
    (void)handle;
    flash_app_t* app = context;
    if (info->source_id != 1U || info->data_type != BLOCK_TYPE ||
        length != FLASH_OFFSET_BYTES + FLASH_BLOCK_BYTES) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (app->staging || (int32_t)(app->now - app->ready_at) < 0) {
        app->busy_returns++;
        return XGL_ERR_BUSY;
    }
    uint32_t offset = update_read_u32(data);
    if (offset != app->committed || offset > IMAGE_BYTES - FLASH_BLOCK_BYTES) {
        return XGL_ERR_INVALID_PARAM;
    }
    memcpy(app->staged, data + FLASH_OFFSET_BYTES, FLASH_BLOCK_BYTES);
    app->staging = true;
    app->ready_at = app->now + 20U;
    if (!app->dropped_ack) {
        /* Transport will send its ACK after this callback returns. */
        app->port->drop_next_tx = true;
        app->dropped_ack = true;
    }
    return XGL_OK;
}

/**
 * \brief           Accept a durable-commit progress notification without
 * reentering
 * \details         A progress message contains a four-byte little-endian
 *                  committed byte count. Accept only monotonic, block-aligned
 *                  progress.
 */
static xgl_error_t host_accept(xgl_handle_t handle, const xgl_rx_info_t* info,
                               const uint8_t* data, size_t length,
                               void* context) {
    (void)handle;
    host_app_t* app = context;
    if (info->source_id != 2U || info->data_type != COMMIT_TYPE ||
        length != 4U) {
        return XGL_ERR_INVALID_PARAM;
    }
    uint32_t committed = update_read_u32(data);
    if (committed > IMAGE_BYTES || committed < app->confirmed ||
        committed % FLASH_BLOCK_BYTES != 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    app->confirmed = committed;
    return XGL_OK;
}

/**
 * \brief           Increment the endpoint's error count without reentry
 * \details         Diagnostic text is borrowed for the callback.
 */
static void count_error(xgl_handle_t handle, xgl_error_t error,
                        const char* message, void* context) {
    (void)handle;
    (void)error;
    (void)message;
    (*(unsigned*)context)++;
}

/**
 * \brief           Simulate a Flash completion and queue its application
 * notification
 */
static void flash_complete(flash_app_t* app) {
    if (app->staging && (int32_t)(app->now - app->ready_at) >= 0) {
        memcpy(app->flash + app->committed, app->staged, FLASH_BLOCK_BYTES);
        app->committed += FLASH_BLOCK_BYTES;
        app->flash_writes++;
        app->staging = false;
        app->notice_pending = true;
    }
}

/** \brief           Run the host-only update simulation without heap
 * allocation. */
int main(void) {
    static sim_node_t host, device;
    static sim_link_t link;
    static uint8_t image[IMAGE_BYTES];
    static flash_app_t flash;
    host_app_t host_app = {0};
    sim_connect(&link);
    for (size_t i = 0; i < sizeof(image); ++i) {
        image[i] = (uint8_t)(i * 17U + 3U);
    }
    memset(flash.flash, 0xFF, sizeof(flash.flash));
    flash.ready_at = 25U; /* Initial erase keeps the receiver BUSY. */
    flash.port = &link.ports[1];
    sim_prepare(&host, 1U);
    sim_prepare(&device, 2U);
    sim_route(&host, 0U, 2U, &link.phys[0]);
    sim_route(&device, 0U, 1U, &link.phys[1]);
    host.config.rx_callback = host_accept;
    host.config.rx_user_data = &host_app;
    device.config.rx_callback = flash_accept;
    device.config.rx_user_data = &flash;
    host.config.error_callback = count_error;
    host.config.callback_user_data = &host_app.errors;
    device.config.error_callback = count_error;
    device.config.callback_user_data = &flash.errors;
    if (sim_init(&host) != XGL_OK || sim_init(&device) != XGL_OK) {
        return 1;
    }
    size_t submitted = 0U;
    for (uint32_t now = 0; now < 10000U && host_app.confirmed < IMAGE_BYTES;
         ++now) {
        flash.now = now;
        flash_complete(&flash);
        if (flash.notice_pending) {
            uint8_t committed[4];
            update_write_u32(committed, (uint32_t)flash.committed);
            const xgl_tx_data_t notice = {.target_id = 1U,
                                          .data_type = COMMIT_TYPE,
                                          .data = committed,
                                          .data_len = sizeof(committed),
                                          .reliable = true};
            xgl_error_t error = xgl_send_at(device.handle, &notice, now);
            if (error == XGL_OK) {
                flash.notice_pending = false;
            } else if (error != XGL_ERR_WINDOW_FULL && error != XGL_ERR_BUSY) {
                return 2;
            }
        }
        /* At most one block is submitted until the device confirms Flash
         * commit. */
        if (submitted == host_app.confirmed && submitted < IMAGE_BYTES) {
            uint8_t block[FLASH_OFFSET_BYTES + FLASH_BLOCK_BYTES];
            update_write_u32(block, (uint32_t)submitted);
            memcpy(block + FLASH_OFFSET_BYTES, image + submitted,
                   FLASH_BLOCK_BYTES);
            const xgl_tx_data_t tx = {.target_id = 2U,
                                      .data_type = BLOCK_TYPE,
                                      .data = block,
                                      .data_len = sizeof(block),
                                      .reliable = true,
                                      .timeout_ms = 100U};
            xgl_error_t error = xgl_send_at(host.handle, &tx, now);
            if (error == XGL_OK) {
                submitted += FLASH_BLOCK_BYTES;
            } else if (error != XGL_ERR_WINDOW_FULL && error != XGL_ERR_BUSY) {
                return 3;
            }
        }
        xgl_error_t host_step = sim_step(&host, now);
        xgl_error_t device_step = sim_step(&device, now);
        /* Flash admission can remain BUSY until the next bounded step. */
        if (host_step != XGL_OK ||
            (device_step != XGL_OK && device_step != XGL_ERR_BUSY)) {
            return 4;
        }
    }
#if XGL_FEATURE_STATISTICS
    xgl_statistics_t statistics = {0};
    (void)xgl_stats_get(host.handle, &statistics);
#endif
    uint16_t expected_crc = image_crc16(image, sizeof(image));
    uint16_t actual_crc = image_crc16(flash.flash, sizeof(flash.flash));
    bool complete = host_app.confirmed == IMAGE_BYTES &&
                    flash.committed == IMAGE_BYTES &&
                    flash.flash_writes == IMAGE_BYTES / FLASH_BLOCK_BYTES &&
                    flash.busy_returns > 0U && flash.port->dropped == 1U &&
                    host_app.errors == 0U && flash.errors == 0U &&
                    expected_crc == actual_crc &&
                    memcmp(image, flash.flash, sizeof(image)) == 0;
#if XGL_FEATURE_STATISTICS
    complete = complete && statistics.tx_retries > 0U;
#endif
    printf("Boot update simulation: %s; blocks=%u, BUSY=%u, dropped_ACK=%u, ",
           complete ? "PASS" : "FAIL", flash.flash_writes, flash.busy_returns,
           flash.port->dropped);
#if XGL_FEATURE_STATISTICS
    printf("retries=%llu, ", (unsigned long long)statistics.tx_retries);
#else
    printf("retries=disabled, ");
#endif
    printf("CRC16=%04X\n", (unsigned)actual_crc);
    xgl_destroy(host.handle);
    xgl_destroy(device.handle);
    return complete ? 0 : 5;
}
