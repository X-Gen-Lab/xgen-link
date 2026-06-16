/**
 * \file            xgl_runtime.c
 * \brief           Protocol runtime scheduling implementation
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>
#include <xgl/internal/xgl_datalink.h>
#include <xgl/internal/xgl_time.h>
#include "xgl_instance_internal.h"

static uint32_t deadline_delta_ms(uint32_t now_ms,
                                  uint32_t start_ms,
                                  uint32_t timeout_ms) {
    uint32_t elapsed_ms = now_ms - start_ms;
    if (elapsed_ms >= timeout_ms) {
        return 0U;
    }

    return timeout_ms - elapsed_ms;
}

static void deadline_take_min(uint32_t* deadline_ms, uint32_t candidate_ms) {
    if (deadline_ms == NULL) {
        return;
    }

    if (candidate_ms < *deadline_ms) {
        *deadline_ms = candidate_ms;
    }
}

static void collect_route_deadlines(const struct xgl_instance* handle,
                                    uint32_t now_ms,
                                    uint32_t* deadline_ms) {
    if (handle == NULL || deadline_ms == NULL) {
        return;
    }

    for (size_t i = 0; i < handle->config.route_table_len; i++) {
        const xgl_route_item_t* route = &handle->config.route_table[i];
        if (route->phy == NULL || route->phy->rx == NULL) {
            continue;
        }

        if (route->read_freq_hz == 0U ||
            handle->route_last_read_ms == NULL ||
            i >= handle->route_last_read_count ||
            handle->route_last_read_ms[i] == 0U) {
            deadline_take_min(deadline_ms, 0U);
            continue;
        }

        uint32_t interval_ms = 1000U / route->read_freq_hz;
        if (interval_ms == 0U) {
            interval_ms = 1U;
        }
        deadline_take_min(deadline_ms,
                          deadline_delta_ms(now_ms,
                                            handle->route_last_read_ms[i],
                                            interval_ms));
    }
}

static void collect_reliable_deadlines(const xgl_transport_ctx_t* transport,
                                       uint32_t now_ms,
                                       uint32_t* deadline_ms) {
    if (transport == NULL || deadline_ms == NULL) {
        return;
    }

    for (const xgl_transport_peer_state_t* peer = transport->peers;
         peer != NULL;
         peer = peer->next) {
        xgl_list_node_t* node;
        XGL_LIST_FOR_EACH(&peer->reliable_queue.wait_ack_list, node) {
            const xgl_reliable_packet_t* packet =
                XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);
            if (packet->send_timestamp == 0U || packet->timeout_ms <= 0) {
                continue;
            }

            deadline_take_min(deadline_ms,
                              deadline_delta_ms(now_ms,
                                                packet->send_timestamp,
                                                (uint32_t)packet->timeout_ms));
        }
    }
}

static void collect_reassembly_deadlines(const xgl_fragment_manager_t* manager,
                                         uint32_t now_ms,
                                         uint32_t* deadline_ms) {
    if (manager == NULL || deadline_ms == NULL) {
        return;
    }

    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&manager->reassembly_list, node) {
        const xgl_reassembly_buffer_t* buffer =
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
        if (buffer->first_fragment_time == 0U || buffer->timeout_ms == 0U) {
            continue;
        }

        deadline_take_min(deadline_ms,
                          deadline_delta_ms(now_ms,
                                            buffer->first_fragment_time,
                                            buffer->timeout_ms));
    }
}

uint32_t xgl_next_deadline_ms(xgl_handle_t handle) {
    if (handle == NULL || !handle->initialized) {
        return XGL_NO_DEADLINE_MS;
    }

    uint32_t now_ms = xgl_time_ms();
    uint32_t deadline_ms = XGL_NO_DEADLINE_MS;

#ifdef XGL_THREAD_SAFE
    if (handle->config.features.thread_safe) {
        xgl_mutex_lock(&handle->mutex);
    }
#endif

    collect_route_deadlines(handle, now_ms, &deadline_ms);
    collect_reliable_deadlines(&handle->layers.transport_ctx,
                               now_ms,
                               &deadline_ms);
    collect_reassembly_deadlines(handle->layers.transport_ctx.fragment_mgr,
                                 now_ms,
                                 &deadline_ms);

#ifdef XGL_THREAD_SAFE
    if (handle->config.features.thread_safe) {
        xgl_mutex_unlock(&handle->mutex);
    }
#endif

    return deadline_ms;
}

void xgl_run(xgl_handle_t handle, uint32_t freq_hz) {
    size_t i;
    uint32_t current_time_ms;

    if (handle == NULL || !handle->initialized) {
        return;
    }

    current_time_ms = xgl_time_ms();

#ifdef XGL_THREAD_SAFE
    if (handle->config.features.thread_safe) {
        xgl_mutex_lock(&handle->mutex);
    }
#endif

    for (i = 0; i < handle->config.route_table_len; i++) {
        xgl_route_item_t* route = &handle->config.route_table[i];
        if (route->phy != NULL && route->phy->rx != NULL) {
            uint32_t route_freq_hz = route->read_freq_hz;
            bool should_read = true;

            if (route_freq_hz > 0U && freq_hz > 0U && route_freq_hz < freq_hz &&
                handle->route_last_read_ms != NULL && i < handle->route_last_read_count) {
                uint32_t interval_ms = 1000U / route_freq_hz;
                if (interval_ms == 0U) {
                    interval_ms = 1U;
                }
                if (handle->route_last_read_ms[i] != 0U &&
                    current_time_ms - handle->route_last_read_ms[i] < interval_ms) {
                    should_read = false;
                }
            }

            if (!should_read) {
                continue;
            }

            if (handle->route_last_read_ms != NULL && i < handle->route_last_read_count) {
                handle->route_last_read_ms[i] = current_time_ms;
            }

            xgl_datalink_receive(&handle->layers.datalink_ctx,
                                 route->phy,
                                 current_time_ms,
                                 1000);
        }
    }

    xgl_transport_run(&handle->layers.transport_ctx, handle, current_time_ms);

#ifdef XGL_THREAD_SAFE
    if (handle->config.features.thread_safe) {
        xgl_mutex_unlock(&handle->mutex);
    }
#endif
}
