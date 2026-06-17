/**
 * \file            xgl_route_mutation.c
 * \brief           Route table mutation helpers
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_hashtable.h>
#include <xgl/internal/xgl_route.h>
#include "xgl_route_table_internal.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Private Helper Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Find route index by target ID
 * \details         Linear search through routes array
 */
static int xgl_route_find_index(const xgl_route_table_t* table,
                                uint16_t target_id) {
    for (size_t i = 0; i < table->route_count; i++) {
        if (table->routes[i].target_id == target_id) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * \brief           Grow routes array capacity
 */
static xgl_error_t xgl_route_grow_capacity(xgl_route_table_t* table) {
    size_t new_capacity = table->route_capacity * 2U;
    if (new_capacity == 0U) {
        new_capacity = XGL_ROUTE_TABLE_DEFAULT_SIZE;
    }

    xgl_route_item_t* new_routes = (xgl_route_item_t*)xgl_route_table_alloc(
        table,
        sizeof(xgl_route_item_t) * new_capacity
    );

    if (new_routes == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    memset(new_routes, 0, sizeof(xgl_route_item_t) * new_capacity);

    if (table->routes != NULL && table->route_count > 0U) {
        memcpy(new_routes, table->routes,
               sizeof(xgl_route_item_t) * table->route_count);
        xgl_route_table_free(table, table->routes);
    }

    table->routes = new_routes;
    table->route_capacity = new_capacity;

    for (size_t i = 0; i < table->route_count; i++) {
        xgl_hashtable_insert(&table->hashtable,
                             table->routes[i].target_id,
                             &table->routes[i]);
    }

    return XGL_OK;
}

/*---------------------------------------------------------------------------*/
/* Public API Implementation                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Add route to table
 */
xgl_error_t xgl_route_table_add(xgl_route_table_t* table,
                                uint16_t target_id,
                                xgl_phy_ops_t* phy,
                                uint16_t max_frame_size,
                                uint32_t read_freq_hz,
                                uint8_t metric) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (phy == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }

    int existing_index = xgl_route_find_index(table, target_id);

    if (existing_index >= 0) {
        xgl_route_item_t* route = &table->routes[existing_index];
        route->phy = phy;
        route->max_frame_size = max_frame_size;
        route->read_freq_hz = read_freq_hz;
        route->metric = metric;

        return xgl_hashtable_insert(&table->hashtable, target_id, route);
    }

    if (table->route_count >= table->route_capacity) {
        xgl_error_t err = xgl_route_grow_capacity(table);
        if (err != XGL_OK) {
            return err;
        }
    }

    xgl_route_item_t* route = &table->routes[table->route_count];
    route->target_id = target_id;
    route->phy = phy;
    route->max_frame_size = max_frame_size;
    route->read_freq_hz = read_freq_hz;
    route->metric = metric;

    xgl_error_t err = xgl_hashtable_insert(&table->hashtable, target_id, route);
    if (err != XGL_OK) {
        return err;
    }

    table->route_count++;

    return XGL_OK;
}

/**
 * \brief           Remove route from table
 */
xgl_error_t xgl_route_table_remove(xgl_route_table_t* table,
                                   uint16_t target_id) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    int index = xgl_route_find_index(table, target_id);
    if (index < 0) {
        return XGL_ERR_ROUTE_NOT_FOUND;
    }

    xgl_error_t err = xgl_hashtable_remove(&table->hashtable, target_id);
    if (err != XGL_OK) {
        return err;
    }

    size_t route_index = (size_t)index;
    if (route_index < table->route_count - 1U) {
        memmove(&table->routes[route_index],
                &table->routes[route_index + 1U],
                sizeof(xgl_route_item_t) * (table->route_count - route_index - 1U));

        for (size_t i = route_index; i < table->route_count - 1U; i++) {
            xgl_hashtable_insert(&table->hashtable,
                                 table->routes[i].target_id,
                                 &table->routes[i]);
        }
    }

    table->route_count--;

    return XGL_OK;
}

/**
 * \brief           Clear all routes from table
 */
void xgl_route_table_clear(xgl_route_table_t* table) {
    if (table == NULL) {
        return;
    }

    xgl_hashtable_clear(&table->hashtable);
    table->route_count = 0;
}
