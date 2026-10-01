/**
 * \file            freertos_port.c
 * \brief           Single-task FreeRTOS integration with caller-owned driver
 * copies
 * \author          X-Gen Lab
 */
#include <xgl/xgl.h>
#ifdef XGL_PORT_FREERTOS_EXAMPLE
#include "FreeRTOS.h"
#include "task.h"

/* Supply a wrap-safe millisecond clock in the board adapter. */
extern uint32_t board_millis(void);
extern xgl_error_t board_tx_copy(const uint8_t* data, size_t length);
extern size_t board_rx_copy_available(uint8_t* data, size_t capacity);

/**
 * \brief           Copy TX bytes into bounded driver-owned DMA storage
 * \param[in]       data: Borrowed frame bytes
 * \param[in]       length: Complete frame length
 * \param[in]       context: Driver context, unused by the skeleton
 * \return          Board driver acceptance status
 */
static xgl_error_t freertos_tx(const uint8_t* data, size_t length,
                               void* context) {
    (void)context;
    return board_tx_copy(data, length);
}

/**
 * \brief           Drain available RX bytes without blocking the protocol task
 * \param[out]      data: Caller RX buffer
 * \param[in,out]   length: Capacity on entry, produced bytes on return
 * \param[in]       context: Driver context, unused by the skeleton
 * \return          XGL_OK
 */
static xgl_error_t freertos_rx(uint8_t* data, size_t* length, void* context) {
    (void)context;
    *length = board_rx_copy_available(data, *length);
    return XGL_OK;
}

xgl_phy_ops_t xgl_freertos_phy = {freertos_tx, freertos_rx, NULL};

/**
 * \brief           Run all protocol operations for one instance in one task
 * \param[in]       context: Handle initialized from storage owned by the
 * application
 * \note            Other tasks and ISRs queue commands; they do not call this
 * handle.
 */
void xgl_freertos_task(void* context) {
    xgl_handle_t handle = context;
    const xgl_work_budget_t budget = {128U, 1000U};
    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        (void)xgl_step(handle, board_millis(), &budget);
        configASSERT(uxTaskGetStackHighWaterMark(NULL) > 64U);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1));
    }
}
#endif
