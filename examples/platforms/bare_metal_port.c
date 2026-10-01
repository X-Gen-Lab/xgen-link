/**
 * \file            bare_metal_port.c
 * \brief           Caller-clock bare-metal integration skeleton
 * \author          X-Gen Lab
 */
#include <xgl/xgl.h>

/* Implement these in the board application. RX must return immediately. */
extern uint32_t board_millis(void);
extern xgl_error_t board_uart_write_sync(const uint8_t* data, size_t length);
extern size_t board_uart_read_available(uint8_t* data, size_t capacity);

/**
 * \brief           Complete transmission or copy bytes into driver-owned
 * storage
 * \param[in]       data: Borrowed frame, invalid after this callback returns
 * \param[in]       length: Frame length
 * \param[in]       context: Board driver context, unused by this skeleton
 * \return          Board driver status
 */
static xgl_error_t bare_metal_tx(const uint8_t* data, size_t length,
                                 void* context) {
    (void)context;
    return board_uart_write_sync(data, length);
}

/**
 * \brief           Read available bytes without waiting for a frame
 * \param[out]      data: Caller RX buffer
 * \param[in,out]   length: Capacity on entry, bytes copied on return
 * \param[in]       context: Board driver context, unused by this skeleton
 * \return          XGL_OK
 */
static xgl_error_t bare_metal_rx(uint8_t* data, size_t* length, void* context) {
    (void)context;
    *length = board_uart_read_available(data, *length);
    return XGL_OK;
}

/** \brief           Public PHY descriptor kept alive by the board application.
 */
xgl_phy_ops_t xgl_bare_metal_phy = {bare_metal_tx, bare_metal_rx, NULL};

/**
 * \brief           Poll an instance initialized in application-owned static
 * workspace
 * \param[in]       handle: Initialized handle; config and descriptors outlive
 * this loop
 * \return          First runtime error
 */
xgl_error_t xgl_bare_metal_poll_example(xgl_handle_t handle) {
    const xgl_work_budget_t budget = {128U, 1000U};
    for (;;) {
        xgl_error_t error = xgl_step(handle, board_millis(), &budget);
        if (error != XGL_OK) {
            return error;
        }
        /* The board may sleep until RX IRQ or xgl_next_timeout() expires. */
    }
}
