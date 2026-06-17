/**
 * \file            xgl_instance_lifecycle.c
 * \brief           Protocol instance creation and destruction
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>
#include <xgl/internal/xgl_allocator.h>
#include <xgl/internal/xgl_tiered_pool.h>
#include <xgl/internal/xgl_packet_pool.h>
#include <xgl/internal/xgl_route.h>
#include "xgl_instance_internal.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Instance Creation                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Create a new protocol instance
 * \details         Allocates memory for instance structure and stores configuration
 */
xgl_handle_t xgl_create(const xgl_config_t* config) {
    xgl_handle_t handle;
    xgl_allocator_t* allocator;
    xgl_error_t err;

    err = xgl_config_validate(config);
    if (err != XGL_OK) {
        return NULL;
    }

    allocator = config->memory.allocator;
    if (allocator == NULL) {
        allocator = xgl_allocator_get_default();
    }

    handle = (xgl_handle_t)xgl_alloc(allocator, sizeof(struct xgl_instance));
    if (handle == NULL) {
        return NULL;
    }

    memset(handle, 0, sizeof(struct xgl_instance));
    memcpy(&handle->config, config, sizeof(xgl_config_t));
    handle->allocator = allocator;
    handle->initialized = false;

    return handle;
}

/*---------------------------------------------------------------------------*/
/* Instance Destruction                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Destroy protocol instance and free all resources
 * \details         Cleans up all allocated resources and frees instance
 */
void xgl_destroy(xgl_handle_t handle) {
    if (handle == NULL) {
        return;
    }

    xgl_transport_destroy(&handle->layers.transport_ctx);

    if (handle->layers.datalink_ctx.rx_cache != NULL) {
        xgl_free(handle->allocator, handle->layers.datalink_ctx.rx_cache);
        handle->layers.datalink_ctx.rx_cache = NULL;
    }

    if (handle->route_last_read_ms != NULL) {
        xgl_free(handle->allocator, handle->route_last_read_ms);
        handle->route_last_read_ms = NULL;
        handle->route_last_read_count = 0;
    }

    xgl_route_table_destroy(&handle->route_table);
    xgl_packet_pool_destroy(&handle->packet_pool);
    xgl_tiered_pool_destroy(&handle->tx_pool);

#ifdef XGL_THREAD_SAFE
    if (handle->config.features.thread_safe) {
        xgl_mutex_destroy(&handle->mutex);
    }
#endif

    xgl_free(handle->allocator, handle);
}
