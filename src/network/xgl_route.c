/**
 * \file            xgl_route.c
 * \brief           Route table implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_allocator.h>
#include <xgl/xgl_error.h>
#include <xgl/internal/xgl_hashtable.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Private Helper Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using table's allocator policy
 */
static void* xgl_route_alloc(xgl_route_table_t* table, size_t size) {
    return xgl_alloc(table != NULL ? table->allocator : NULL, size);
}

/**
 * \brief           Free memory using table's allocator policy
 */
static void xgl_route_free(xgl_route_table_t* table, void* ptr) {
    xgl_free(table != NULL ? table->allocator : NULL, ptr);
}

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
    /* Double the capacity */
    size_t new_capacity = table->route_capacity * 2;
    if (new_capacity == 0) {
        new_capacity = XGL_ROUTE_TABLE_DEFAULT_SIZE;
    }

    /* Allocate new array */
    xgl_route_item_t* new_routes = (xgl_route_item_t*)xgl_route_alloc(
        table,
        sizeof(xgl_route_item_t) * new_capacity
    );

    if (new_routes == NULL) {
        return XGL_ERR_NO_MEMORY;
    }

    /* Initialize new array to zero */
    memset(new_routes, 0, sizeof(xgl_route_item_t) * new_capacity);

    /* Copy existing routes */
    if (table->routes != NULL && table->route_count > 0) {
        memcpy(new_routes, table->routes,
               sizeof(xgl_route_item_t) * table->route_count);
        xgl_route_free(table, table->routes);
    }

    /* Update table */
    table->routes = new_routes;
    table->route_capacity = new_capacity;

    /* CRITICAL: Update all hashtable pointers to point to new array */
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
 * \brief           Initialize route table
 */
xgl_error_t xgl_route_table_init(xgl_route_table_t* table,
                                 size_t initial_capacity,
                                 xgl_allocator_t* allocator) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    /* Initialize structure */
    memset(table, 0, sizeof(xgl_route_table_t));
    table->allocator = allocator;

    /* Initialize hash table for O(1) lookup */
    xgl_error_t err = xgl_hashtable_init(&table->hashtable,
                                         XGL_ROUTE_TABLE_DEFAULT_SIZE,
                                         allocator);
    if (err != XGL_OK) {
        return err;
    }

    /* Allocate initial routes array */
    if (initial_capacity > 0) {
        table->routes = (xgl_route_item_t*)xgl_route_alloc(
            table,
            sizeof(xgl_route_item_t) * initial_capacity
        );

        if (table->routes == NULL) {
            xgl_hashtable_destroy(&table->hashtable);
            return XGL_ERR_NO_MEMORY;
        }

        /* Initialize routes array to zero */
        memset(table->routes, 0, sizeof(xgl_route_item_t) * initial_capacity);

        table->route_capacity = initial_capacity;
    }

    table->route_count = 0;

    return XGL_OK;
}

/**
 * \brief           Destroy route table and free resources
 */
void xgl_route_table_destroy(xgl_route_table_t* table) {
    if (table == NULL) {
        return;
    }

    /* Destroy hash table */
    xgl_hashtable_destroy(&table->hashtable);

    /* Free routes array */
    if (table->routes != NULL) {
        xgl_route_free(table, table->routes);
        table->routes = NULL;
    }

    /* Reset structure */
    table->route_count = 0;
    table->route_capacity = 0;
}

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

    /* Check if route already exists */
    int existing_index = xgl_route_find_index(table, target_id);

    if (existing_index >= 0) {
        /* Update existing route */
        xgl_route_item_t* route = &table->routes[existing_index];
        route->phy = phy;
        route->max_frame_size = max_frame_size;
        route->read_freq_hz = read_freq_hz;
        route->metric = metric;

        /* Update hash table entry (points to same route) */
        return xgl_hashtable_insert(&table->hashtable, target_id, route);
    }

    /* Grow capacity if needed */
    if (table->route_count >= table->route_capacity) {
        xgl_error_t err = xgl_route_grow_capacity(table);
        if (err != XGL_OK) {
            return err;
        }
    }

    /* Add new route */
    xgl_route_item_t* route = &table->routes[table->route_count];
    route->target_id = target_id;
    route->phy = phy;
    route->max_frame_size = max_frame_size;
    route->read_freq_hz = read_freq_hz;
    route->metric = metric;

    /* Insert into hash table */
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

    /* Find route index */
    int index = xgl_route_find_index(table, target_id);
    if (index < 0) {
        return XGL_ERR_ROUTE_NOT_FOUND;
    }

    /* Remove from hash table */
    xgl_error_t err = xgl_hashtable_remove(&table->hashtable, target_id);
    if (err != XGL_OK) {
        return err;
    }

    /* Remove from routes array by shifting remaining routes */
    size_t route_index = (size_t)index;
    if (route_index < table->route_count - 1U) {
        memmove(&table->routes[route_index],
                &table->routes[route_index + 1U],
                sizeof(xgl_route_item_t) * (table->route_count - route_index - 1U));

        /* Update hash table pointers for shifted routes */
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

    /* Clear hash table */
    xgl_hashtable_clear(&table->hashtable);

    /* Reset route count */
    table->route_count = 0;
}

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

    /* Clear existing routes */
    xgl_route_table_clear(table);

    /* Add each route */
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
