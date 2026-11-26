/**
 * \file            xgl_instance_links.c
 * \brief           Per-PHY receive parser ownership and route polling intervals
 * \author          X-Gen Lab
 */

#include "internal/xgl_allocator.h"
#include <string.h>

#include "xgl_instance_internal.h"

/**
 * \brief           Identify a PHY's first occurrence in immutable configuration
 */
static bool link_config_first_phy(const xgl_config_t* config, size_t index) {
    for (size_t i = 0U; i < index; ++i) {
        if (config->route_table[i].phy == config->route_table[index].phy) {
            return false;
        }
    }
    return true;
}

/**
 * \brief           Merge initialization hints into one PHY polling interval
 */
static uint32_t link_config_interval(const xgl_config_t* config,
                                     size_t first_route) {
    uint32_t frequency = 1U;
    const xgl_phy_ops_t* phy = config->route_table[first_route].phy;
    for (size_t i = first_route; i < config->route_table_len; ++i) {
        const xgl_route_item_t* route = &config->route_table[i];
        if (route->phy != phy) {
            continue;
        }
        if (route->read_freq_hz == 0U) {
            return 0U;
        }
        if (route->read_freq_hz > frequency) {
            frequency = route->read_freq_hz;
        }
    }
    /* The largest positive frequency gives the shortest polling interval. */
    uint32_t interval = 1000U / frequency;
    return interval != 0U ? interval : 1U;
}

/**
 * \brief           Count unique PHY descriptors for both planning and setup
 */
size_t xgl_instance_count_links(const xgl_config_t* config) {
    size_t count = 0U;
    for (size_t i = 0U; i < config->route_table_len; ++i) {
        if (link_config_first_phy(config, i)) {
            ++count;
        }
    }
    return count;
}

/**
 * \brief           Release every link's receive storage and descriptors
 */
void xgl_instance_destroy_links(xgl_handle_t handle) {
    if (handle == NULL) {
        return;
    }
    for (size_t i = 0; i < handle->link_count; ++i) {
        xgl_free(handle->allocator, handle->links[i].parser.cache);
    }
    xgl_free(handle->allocator, handle->links);
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
        xgl_alloc(handle->allocator, count * sizeof(*handle->links));
    if (handle->links == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    memset(handle->links, 0, count * sizeof(*handle->links));
    for (size_t i = 0; i < handle->config->route_table_len; ++i) {
        if (!link_config_first_phy(handle->config, i)) {
            continue;
        }
        xgl_instance_link_t* link = &handle->links[handle->link_count++];
        link->phy = handle->config->route_table[i].phy;
        link->poll_interval_ms = link_config_interval(handle->config, i);
        link->parser.cache =
            xgl_alloc(handle->allocator, handle->config->memory.rx_buffer_size);
        if (link->parser.cache == NULL) {
            xgl_instance_destroy_links(handle);
            return XGL_ERR_NO_MEMORY;
        }
        xgl_error_t error =
            xgl_parser_init(&link->parser, link->parser.cache,
                            handle->config->protocol.max_frame_size);
        if (error != XGL_OK) {
            xgl_instance_destroy_links(handle);
            return error;
        }
    }
    return XGL_OK;
}
