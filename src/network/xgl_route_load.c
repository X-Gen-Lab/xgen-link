/**
 * \file            xgl_route_load.c
 * \brief           Route table bulk loading helpers
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_route.h>

/**
 * \brief           Load routes from configuration
 */
xgl_error_t xgl_route_table_load(xgl_route_table_t* table,
                                 const xgl_route_item_t* routes,
                                 size_t count) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (routes == NULL && count > 0) {
        return XGL_ERR_INVALID_PARAM;
    }

    xgl_route_table_clear(table);

    for (size_t i = 0; i < count; i++) {
        xgl_error_t err = xgl_route_table_add(table,
                                             routes[i].target_id,
                                             routes[i].phy,
                                             routes[i].max_frame_size,
                                             routes[i].read_freq_hz,
                                             routes[i].metric);
        if (err != XGL_OK) {
            return err;
        }
    }

    return XGL_OK;
}
