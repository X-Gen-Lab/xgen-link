/**
 * \file            main.c
 * \brief           Static fixed-block update with slow Flash admission and lost
 * ACK recovery
 * \author          X-Gen Lab
 */
#include <stdio.h>
#include <xgen/bytes/bytes.h>
#include <xgen/crc/crc.h>

#include "../common/sim_link.h"

#define IMAGE_BYTES       512U
#define FLASH_BLOCK_BYTES 64U
#define BLOCK_TYPE        7U
#define COMMIT_TYPE       8U

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

/**
 * \brief           Admit one copied block; Flash writes happen outside this
 * callback
 * \param[in]       handle: Callback instance, unused
 * \param[in]       source: Host identifier
 * \param[in]       type: Block message type
 * \param[in]       data: Little-endian offset followed by exactly 64 bytes
 * \param[in]       length: Fixed block message length
 * \param[in,out]   context: Bounded Flash staging slot
 * \return          BUSY until the slot is available, OK after copying ownership
 */
static xgl_error_t flash_accept(xgl_handle_t handle, uint16_t source,
                                uint8_t type, const uint8_t* data,
                                size_t length, void* context) {
    (void)handle;
    flash_app_t* app = context;
    if (source != 1U || type != BLOCK_TYPE ||
        length != 4U + FLASH_BLOCK_BYTES) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (app->staging || (int32_t)(app->now - app->ready_at) < 0) {
        app->busy_returns++;
        return XGL_ERR_BUSY;
    }
    uint32_t offset = xgb_deserialize_u32_le(data);
    if (offset != app->committed || offset > IMAGE_BYTES - FLASH_BLOCK_BYTES) {
        return XGL_ERR_INVALID_PARAM;
    }
    memcpy(app->staged, data + 4U, FLASH_BLOCK_BYTES);
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
 * \param[in]       handle: Callback instance, unused
 * \param[in]       source: Device identifier
 * \param[in]       type: Commit message type
 * \param[in]       data: Little-endian committed byte count
 * \param[in]       length: Must be four
 * \param[in,out]   context: Host progress state
 * \return          XGL_OK for monotonic block-aligned progress
 */
static xgl_error_t host_accept(xgl_handle_t handle, uint16_t source,
                               uint8_t type, const uint8_t* data, size_t length,
                               void* context) {
    (void)handle;
    host_app_t* app = context;
    if (source != 2U || type != COMMIT_TYPE || length != 4U) {
        return XGL_ERR_INVALID_PARAM;
    }
    uint32_t committed = xgb_deserialize_u32_le(data);
    if (committed > IMAGE_BYTES || committed < app->confirmed ||
        committed % FLASH_BLOCK_BYTES != 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    app->confirmed = committed;
    return XGL_OK;
}

/**
 * \brief           Record a host error without reentering the instance
 * \param[in]       handle: Callback instance, unused
 * \param[in]       error: Reported protocol failure
 * \param[in]       message: Borrowed diagnostic text
 * \param[in,out]   context: Host application state
 */
static void host_error(xgl_handle_t handle, xgl_error_t error,
                       const char* message, void* context) {
    (void)handle;
    (void)error;
    (void)message;
    ((host_app_t*)context)->errors++;
}

/**
 * \brief           Record a device error without reentering the instance
 * \param[in]       handle: Callback instance, unused
 * \param[in]       error: Reported protocol failure
 * \param[in]       message: Borrowed diagnostic text
 * \param[in,out]   context: Flash application state
 */
static void flash_error(xgl_handle_t handle, xgl_error_t error,
                        const char* message, void* context) {
    (void)handle;
    (void)error;
    (void)message;
    ((flash_app_t*)context)->errors++;
}

/**
 * \brief           Simulate a Flash completion and queue its application
 * notification
 * \param[in,out]   app: Flash staging state
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
    static sim_channel_t to_host, to_device;
    static uint8_t image[IMAGE_BYTES];
    static flash_app_t flash;
    host_app_t host_app = {0};
    sim_port_t host_port = {&to_host, &to_device, false, 0};
    sim_port_t device_port = {&to_device, &to_host, false, 0};
    xgl_phy_ops_t host_phy = {sim_tx, sim_rx, &host_port};
    xgl_phy_ops_t device_phy = {sim_tx, sim_rx, &device_port};
    for (size_t i = 0; i < sizeof(image); ++i) {
        image[i] = (uint8_t)(i * 17U + 3U);
    }
    memset(flash.flash, 0xFF, sizeof(flash.flash));
    flash.ready_at = 25U; /* Initial erase keeps the receiver BUSY. */
    flash.port = &device_port;
    sim_prepare(&host, 1U);
    sim_prepare(&device, 2U);
    sim_route(&host, 0U, 2U, &host_phy);
    sim_route(&device, 0U, 1U, &device_phy);
    host.config.rx_accept_callback = host_accept;
    host.config.callback_user_data = &host_app;
    device.config.rx_accept_callback = flash_accept;
    device.config.callback_user_data = &flash;
    host.config.error_callback = host_error;
    device.config.error_callback = flash_error;
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
            xgb_serialize_u32_le(committed, (uint32_t)flash.committed);
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
            uint8_t block[4U + FLASH_BLOCK_BYTES];
            xgb_serialize_u32_le(block, (uint32_t)submitted);
            memcpy(block + 4U, image + submitted, FLASH_BLOCK_BYTES);
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
        if (sim_step(&host, now) != XGL_OK ||
            sim_step(&device, now) != XGL_OK) {
            return 4;
        }
    }
    xgl_statistics_t statistics = {0};
    (void)xgl_stats_get(host.handle, &statistics);
    uint16_t expected_crc = xgcrc_crc16_modbus(image, sizeof(image));
    uint16_t actual_crc = xgcrc_crc16_modbus(flash.flash, sizeof(flash.flash));
    bool complete = host_app.confirmed == IMAGE_BYTES &&
                    flash.committed == IMAGE_BYTES &&
                    flash.flash_writes == IMAGE_BYTES / FLASH_BLOCK_BYTES &&
                    flash.busy_returns > 0U && device_port.dropped == 1U &&
                    host_app.errors == 0U && flash.errors == 0U &&
                    statistics.tx_retries > 0U && expected_crc == actual_crc &&
                    memcmp(image, flash.flash, sizeof(image)) == 0;
    printf("Boot update simulation: %s; blocks=%u, BUSY=%u, dropped_ACK=%u, "
           "retries=%llu, CRC16=%04X\n",
           complete ? "PASS" : "FAIL", flash.flash_writes, flash.busy_returns,
           device_port.dropped, (unsigned long long)statistics.tx_retries,
           (unsigned)actual_crc);
    xgl_destroy(host.handle);
    xgl_destroy(device.handle);
    return complete ? 0 : 5;
}
