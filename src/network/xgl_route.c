/**
 * \file            xgl_route.c
 * \brief           Route table implementation
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_route.h>
#include <xgl/internal/xgl_allocator.h>
#include <xgl/xgl_error.h>
#include <xgl/internal/xgl_hashtable.h>
#include "xgl_route_table_internal.h"
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Private Helper Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using table's allocator policy
 */
void* xgl_route_table_alloc(xgl_route_table_t* table, size_t size) {
    return xgl_alloc(table != NULL ? table->allocator : NULL, size);
}

/**
 * \brief           Free memory using table's allocator policy
 */
void xgl_route_table_free(xgl_route_table_t* table, void* ptr) {
    xgl_free(table != NULL ? table->allocator : NULL, ptr);
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
        table->routes = (xgl_route_item_t*)xgl_route_table_alloc(
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
        xgl_route_table_free(table, table->routes);
        table->routes = NULL;
    }

    /* Reset structure */
    table->route_count = 0;
    table->route_capacity = 0;
}
