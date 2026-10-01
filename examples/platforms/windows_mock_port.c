/**
 * \file            windows_mock_port.c
 * \brief           Windows mock PHY for host-side SDK tests
 */

#include <string.h>

#include "xgl/xgl.h"

typedef struct {
    uint8_t buffer[1024];
    size_t len;
    unsigned tx_count;
    unsigned rx_count;
} xgl_windows_mock_phy_t;

/**
 * \brief           Copy a frame without overwriting unread bytes
 * \param[in]       data: Borrowed frame bytes
 * \param[in]       len: Frame length
 * \param[in,out]   user_data: Persistent bounded mock state
 * \return          XGL_OK after copying, error code on invalid input or
 * overflow
 */
static xgl_error_t windows_mock_tx(const uint8_t* data, size_t len,
                                   void* user_data) {
    xgl_windows_mock_phy_t* phy = (xgl_windows_mock_phy_t*)user_data;
    if (phy == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (len > sizeof(phy->buffer) - phy->len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(phy->buffer + phy->len, data, len);
    phy->len += len;
    phy->tx_count++;
    return XGL_OK;
}

/**
 * \brief           Copy a bounded prefix and preserve remaining RX bytes
 * \param[out]      buffer: Caller RX buffer
 * \param[in,out]   len: Capacity on entry, produced length on return
 * \param[in,out]   user_data: Persistent bounded mock state
 * \return          XGL_OK after reading, error code on invalid input
 */
static xgl_error_t windows_mock_rx(uint8_t* buffer, size_t* len,
                                   void* user_data) {
    xgl_windows_mock_phy_t* phy = (xgl_windows_mock_phy_t*)user_data;
    if (phy == NULL || buffer == NULL || len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (phy->len == 0U) {
        *len = 0;
        return XGL_OK;
    }
    if (*len > phy->len) {
        *len = phy->len;
    }
    memcpy(buffer, phy->buffer, *len);
    phy->len -= *len;
    memmove(phy->buffer, phy->buffer + *len, phy->len);
    phy->rx_count++;
    return XGL_OK;
}

void xgl_windows_mock_phy_init(xgl_phy_ops_t* ops,
                               xgl_windows_mock_phy_t* phy) {
    if (ops == NULL || phy == NULL) {
        return;
    }

    memset(phy, 0, sizeof(*phy));
    ops->tx = windows_mock_tx;
    ops->rx = windows_mock_rx;
    ops->user_data = phy;
}
