/**
 * \file            freertos_port.c
 * \brief           FreeRTOS xgen-link porting skeleton
 */

#include "xgl/xgl.h"

#ifdef XGL_PORT_FREERTOS_EXAMPLE
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

typedef struct {
    QueueHandle_t tx_queue;
    QueueHandle_t rx_queue;
} xgl_freertos_phy_t;

static xgl_error_t freertos_tx(const uint8_t* data, size_t len, void* user_data) {
    xgl_freertos_phy_t* phy = (xgl_freertos_phy_t*)user_data;
    if (phy == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    return xQueueSend(phy->tx_queue, &data, 0) == pdPASS ? XGL_OK : XGL_ERR_QUEUE_FULL;
}

static xgl_error_t freertos_rx(uint8_t* buffer, size_t* len, void* user_data) {
    xgl_freertos_phy_t* phy = (xgl_freertos_phy_t*)user_data;
    if (phy == NULL || buffer == NULL || len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    size_t rx_len = 0;
    if (xQueueReceive(phy->rx_queue, &rx_len, 0) != pdPASS) {
        *len = 0;
        return XGL_OK;
    }

    if (rx_len > *len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    *len = rx_len;
    return XGL_OK;
}

static void xgl_task(void* arg) {
    xgl_handle_t handle = (xgl_handle_t)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        xgl_run(handle, 100);
        configASSERT(uxTaskGetStackHighWaterMark(NULL) > 64U);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

void xgl_freertos_start(xgl_handle_t handle) {
    (void)xTaskCreate(xgl_task, "xgl", 512, handle, tskIDLE_PRIORITY + 1, NULL);
}
#endif
