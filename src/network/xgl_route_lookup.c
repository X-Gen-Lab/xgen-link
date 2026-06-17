/**
 * \file            xgl_route_lookup.c
 * \brief           Route table lookup helpers
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_hashtable.h>

/**
 * \brief           Lookup route in table (O(1) average)
 */
xgl_route_item_t* xgl_route_table_lookup(const xgl_route_table_t* table,
                                         uint16_t target_id) {
    if (table == NULL) {
        return NULL;
    }

    return xgl_hashtable_lookup(&table->hashtable, target_id);
}

/**
 * \brief           Update route metric
 */
// cppcheck-suppress constParameterPointer
xgl_error_t xgl_route_table_update_metric(xgl_route_table_t* table,
                                          uint16_t target_id,
                                          uint8_t metric) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    xgl_route_item_t* route = xgl_route_table_lookup(table, target_id);
    if (route == NULL) {
        return XGL_ERR_ROUTE_NOT_FOUND;
    }

    route->metric = metric;

    return XGL_OK;
}
