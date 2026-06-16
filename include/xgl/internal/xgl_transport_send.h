/**
 * \file            xgl_transport_send.h
 * \brief           Internal transport send planning helpers
 */

#ifndef XGL_TRANSPORT_SEND_H
#define XGL_TRANSPORT_SEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xgl/internal/xgl_transport.h"

typedef struct {
    uint16_t max_frame_size;
    size_t app_extensions_len;
    size_t app_payload_budget;
    bool needs_fragmentation;
    size_t fragment_extensions_len;
    size_t fragment_payload_budget;
    size_t fragment_count;
} xgl_transport_send_plan_t;

xgl_error_t transport_build_send_plan(const xgl_transport_ctx_t* ctx,
                                      const xgl_tx_data_t* tx_data,
                                      xgl_transport_send_plan_t* plan);

#ifdef __cplusplus
}
#endif

#endif /* XGL_TRANSPORT_SEND_H */
