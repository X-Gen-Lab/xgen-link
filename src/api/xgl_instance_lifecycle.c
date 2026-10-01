/**
 * \file            xgl_instance_lifecycle.c
 * \brief           Protocol instance creation and destruction
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>

#include <xgen/memory/allocator.h>
#if XGL_ALLOW_FALLBACK_MALLOC
#include <xgen/memory/libc_allocator.h>
#endif
#include <xgl/internal/xgl_route.h>

#include "xgl_instance_internal.h"

/*---------------------------------------------------------------------------*/
/* Instance Creation                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Reserve one complete workspace through an explicit backend
 * \details         The immutable configuration is borrowed until destruction.
 *                  Protocol initialization is deferred to xgl_init().
 */
xgl_handle_t xgl_create_checked(const xgl_config_t* config, size_t config_size,
                                uint32_t abi_version,
                                uint32_t build_config_id) {
    xgl_memory_requirements_t requirements;
    xgl_error_t err = xgl_memory_requirements_checked(
        config, config_size, abi_version, build_config_id, &requirements);
    if (err != XGL_OK) {
        return NULL;
    }

    const xgm_allocator_t* allocator = config->memory.allocator;
#if XGL_ALLOW_FALLBACK_MALLOC
    if (allocator == NULL) {
        allocator = xgm_allocator_libc();
    }
#endif
    if (!xgm_allocator_is_valid(allocator)) {
        return NULL;
    }

    void* storage = xgm_alloc(allocator, requirements.size);
    if (storage == NULL) {
        return NULL;
    }
    xgl_handle_t handle;
    err = xgl_workspace_prepare(config, storage, requirements.size, allocator,
                                &handle);
    if (err != XGL_OK) {
        xgm_free(allocator, storage);
        return NULL;
    }
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
    xgl_instance_destroy_links(handle);

    if (handle->layers.datalink_ctx.rx_cache != NULL) {
        xgm_free(handle->allocator, handle->layers.datalink_ctx.rx_cache);
        handle->layers.datalink_ctx.rx_cache = NULL;
    }

    xgl_route_table_destroy(&handle->route_table);

    handle->initialized = false;
    if (!handle->caller_owned) {
        xgm_free(handle->storage_allocator, handle);
    }
}
