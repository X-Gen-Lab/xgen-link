/**
 * \file            xgl_security.h
 * \brief           Security helpers for authenticated production transport
 */

#ifndef XGL_SECURITY_H
#define XGL_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "xgl_error.h"

typedef struct {
    uint16_t source_id;
    uint32_t connection_id;
    uint32_t session_epoch;
    uint32_t largest_packet_number;
    uint64_t received_bitmap;
    uint8_t window_size;
    bool has_largest;
} xgl_replay_window_t;

xgl_error_t xgl_replay_window_init(xgl_replay_window_t* window,
                                   uint16_t source_id,
                                   uint32_t connection_id,
                                   uint32_t session_epoch,
                                   uint8_t window_size);

bool xgl_replay_window_accept(xgl_replay_window_t* window,
                              uint16_t source_id,
                              uint32_t connection_id,
                              uint32_t session_epoch,
                              uint32_t packet_number);

#ifdef __cplusplus
}
#endif

#endif /* XGL_SECURITY_H */
