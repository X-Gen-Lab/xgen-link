/**
 * \file            xgl_fragment.c
 * \brief           Packet Fragmentation and Reassembly Implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_fragment.h>

/*---------------------------------------------------------------------------*/
/* Fragmentation Manager Functions                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize fragmentation manager
 */
xgl_error_t xgl_fragment_init(xgl_fragment_manager_t *manager,
                              size_t max_reassembly_buffers,
                              uint32_t reassembly_timeout_ms,
                              xgl_allocator_t *allocator)
{
    if (manager == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Initialize reassembly list */
    xgl_list_init(&manager->reassembly_list);

    manager->next_message_id = 0;

    /* Store configuration */
    manager->max_reassembly_buffers = max_reassembly_buffers;
    manager->reassembly_timeout_ms = reassembly_timeout_ms;
    manager->allocator = allocator;
    manager->max_message_size = 0;
    manager->max_reassembly_bytes = 0;
    manager->current_reassembly_bytes = 0;

    return XGL_OK;
}

xgl_error_t xgl_fragment_set_limits(xgl_fragment_manager_t *manager,
                                    size_t max_message_size,
                                    size_t max_reassembly_bytes)
{
    if (manager == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (max_reassembly_bytes != 0U && max_message_size > max_reassembly_bytes) {
        return XGL_ERR_INVALID_PARAM;
    }

    manager->max_message_size = max_message_size;
    manager->max_reassembly_bytes = max_reassembly_bytes;

    return XGL_OK;
}

/**
 * \brief           Destroy fragmentation manager
 */
void xgl_fragment_destroy(xgl_fragment_manager_t *manager)
{
    if (manager == NULL) {
        return;
    }

    /* Clear all reassembly buffers */
    xgl_fragment_clear_reassembly(manager);
}
