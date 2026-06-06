/**
 * \file            windows_mock_port.c
 * \brief           Windows mock PHY for host-side SDK tests
 */

#include "xgl/xgl.h"
#include <string.h>

typedef struct {
    uint8_t buffer[1024];
    size_t len;
    unsigned tx_count;
    unsigned rx_count;
} xgl_windows_mock_phy_t;

static xgl_error_t windows_mock_tx(const uint8_t* data, size_t len, void* user_data) {
    xgl_windows_mock_phy_t* phy = (xgl_windows_mock_phy_t*)user_data;
    if (phy == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (len > sizeof(phy->buffer)) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(phy->buffer, data, len);
    phy->len = len;
    phy->tx_count++;
    return XGL_OK;
}

static xgl_error_t windows_mock_rx(uint8_t* buffer, size_t* len, void* user_data) {
    xgl_windows_mock_phy_t* phy = (xgl_windows_mock_phy_t*)user_data;
    if (phy == NULL || buffer == NULL || len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (phy->len == 0U) {
        *len = 0;
        return XGL_OK;
    }
    if (*len < phy->len) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(buffer, phy->buffer, phy->len);
    *len = phy->len;
    phy->len = 0;
    phy->rx_count++;
    return XGL_OK;
}

void xgl_windows_mock_phy_init(xgl_phy_ops_t* ops, xgl_windows_mock_phy_t* phy) {
    if (ops == NULL || phy == NULL) {
        return;
    }

    memset(phy, 0, sizeof(*phy));
    ops->tx = windows_mock_tx;
    ops->rx = windows_mock_rx;
    ops->user_data = phy;
}
