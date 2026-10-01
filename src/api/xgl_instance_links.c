/**
 * \file            xgl_instance_links.c
 * \brief           Per-PHY receive parser ownership and route polling intervals
 * \author          X-Gen Lab
 */

#include <string.h>
#include <xgen/memory/allocator.h>

#include "xgl_instance_internal.h"

/**
 * \brief           Count unique PHY descriptors for both planning and setup
 */
size_t xgl_instance_count_links(const xgl_config_t* config) {
    size_t count = 0U;
    for (size_t i = 0U; i < config->route_table_len; ++i) {
        size_t j = 0U;
        for (; j < i; ++j) {
            if (config->route_table[j].phy == config->route_table[i].phy) {
                break;
            }
        }
        if (j == i) {
            ++count;
        }
    }
    return count;
}

/**
 * \brief           Release link storage and secondary receive caches
 */
void xgl_instance_destroy_links(xgl_handle_t handle) {
    if (handle == NULL) {
        return;
    }
    for (size_t i = 0; i < handle->link_count; ++i) {
        /* The first cache is owned by the datalink compatibility context. */
        if (handle->links[i].rx_cache != handle->layers.datalink_ctx.rx_cache) {
            xgm_free(handle->allocator, handle->links[i].rx_cache);
        }
    }
    xgm_free(handle->allocator, handle->links);
    handle->links = NULL;
    handle->link_count = 0U;
}

/**
 * \brief           Deduplicate route PHYs and create one parser per link
 */
xgl_error_t xgl_instance_init_links(xgl_handle_t handle) {
    const size_t count = xgl_instance_count_links(handle->config);
    if (count == 0U) {
        return XGL_OK;
    }
    if (count > SIZE_MAX / sizeof(*handle->links)) {
        return XGL_ERR_INVALID_PARAM;
    }
    handle->links =
        xgm_alloc(handle->allocator, count * sizeof(*handle->links));
    if (handle->links == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(handle->links, 0, count * sizeof(*handle->links));
    for (size_t i = 0; i < handle->config->route_table_len; ++i) {
        const xgl_route_item_t* route = &handle->config->route_table[i];
        uint32_t interval =
            route->read_freq_hz != 0U ? 1000U / route->read_freq_hz : 0U;
        if (route->read_freq_hz != 0U && interval == 0U) {
            interval = 1U;
        }
        size_t index = 0U;
        while (index < handle->link_count &&
               handle->links[index].phy != route->phy) {
            ++index;
        }
        if (index < handle->link_count) {
            if (route->read_freq_hz > handle->links[index].read_freq_hz) {
                handle->links[index].read_freq_hz = route->read_freq_hz;
            }
            if (interval < handle->links[index].poll_interval_ms) {
                handle->links[index].poll_interval_ms = interval;
            }
            continue;
        }
        xgl_instance_link_t* link = &handle->links[handle->link_count++];
        link->phy = route->phy;
        link->poll_interval_ms = interval;
        link->read_freq_hz = route->read_freq_hz;
        link->rx_cache = index == 0U
                             ? handle->layers.datalink_ctx.rx_cache
                             : xgm_alloc(handle->allocator,
                                         handle->config->memory.rx_buffer_size);
        if (link->rx_cache == NULL) {
            xgl_instance_destroy_links(handle);
            return XGL_ERR_NO_MEMORY;
        }
        xgl_error_t error =
            xgl_parser_init(&link->parser, link->rx_cache,
                            handle->config->protocol.max_frame_size);
        if (error != XGL_OK) {
            xgl_instance_destroy_links(handle);
            return error;
        }
    }
    return XGL_OK;
}
