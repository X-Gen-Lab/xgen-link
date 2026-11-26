/**
 * \file            xgl_route.c
 * \brief           Fixed-capacity protocol routes with an optional index
 * \author          X-Gen Lab
 */

#include <network/xgl_route.h>

#include "internal/xgl_allocator.h"
#include <string.h>

/**
 * \brief           Reserve all routes without runtime growth
 */
xgl_error_t xgl_route_table_init(xgl_route_table_t* table, size_t capacity,
                                 const xgl_allocator_t* allocator) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    memset(table, 0, sizeof(*table));
    if (!xgl_allocator_is_valid(allocator) || capacity > UINT16_MAX ||
        capacity > SIZE_MAX / sizeof(*table->routes)) {
        return XGL_ERR_INVALID_PARAM;
    }
    table->allocator = allocator;
    if (capacity == 0U) {
        return XGL_OK;
    }
    table->routes = xgl_alloc(allocator, capacity * sizeof(*table->routes));
    if (table->routes == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
#if XGL_FEATURE_ROUTE_INDEX
    if (capacity > SIZE_MAX / sizeof(*table->nodes)) {
        xgl_route_table_destroy(table);
        return XGL_ERR_INVALID_PARAM;
    }
    table->nodes = xgl_alloc(allocator, capacity * sizeof(*table->nodes));
    xgl_route_index_node_t** buckets = (xgl_route_index_node_t**)xgl_alloc(
        allocator, XGL_ROUTE_TABLE_DEFAULT_SIZE * sizeof(*buckets));
    if (table->nodes == NULL || buckets == NULL) {
        xgl_free(allocator, (void*)buckets);
        xgl_route_table_destroy(table);
        return XGL_ERR_NO_MEMORY;
    }
    if (!xgl_route_index_init(&table->index, buckets,
                              XGL_ROUTE_TABLE_DEFAULT_SIZE, capacity)) {
        xgl_free(allocator, (void*)buckets);
        xgl_route_table_destroy(table);
        return XGL_ERR_INVALID_PARAM;
    }
#endif
    table->route_capacity = capacity;
    return XGL_OK;
}

/**
 * \brief           Release reserved routes through their owning allocator
 */
void xgl_route_table_destroy(xgl_route_table_t* table) {
    if (table == NULL) {
        return;
    }
#if XGL_FEATURE_ROUTE_INDEX
    xgl_route_index_clear(&table->index);
    xgl_free(table->allocator, (void*)table->index.buckets);
    xgl_free(table->allocator, table->nodes);
#endif
    xgl_free(table->allocator, table->routes);
    memset(table, 0, sizeof(*table));
}

/**
 * \brief           Find a destination in the admitted route set
 */
xgl_route_item_t* xgl_route_table_lookup(const xgl_route_table_t* table,
                                         uint16_t target_id) {
    if (table == NULL) {
        return NULL;
    }
#if XGL_FEATURE_ROUTE_INDEX
    return xgl_route_index_find(&table->index, target_id);
#else
    for (size_t i = 0U; i < table->route_count; ++i) {
        if (table->routes[i].target_id == target_id) {
            return &table->routes[i];
        }
    }
    return NULL;
#endif
}

/**
 * \brief           Update a destination or use a reserved route slot
 */
xgl_error_t xgl_route_table_add(xgl_route_table_t* table, uint16_t target_id,
                                xgl_phy_ops_t* phy, uint16_t max_frame_size,
                                uint32_t read_freq_hz, uint8_t metric) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (phy == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    xgl_route_item_t* route = xgl_route_table_lookup(table, target_id);
    if (route == NULL) {
        if (table->route_count >= table->route_capacity) {
            return XGL_ERR_QUEUE_FULL;
        }
        route = &table->routes[table->route_count];
#if XGL_FEATURE_ROUTE_INDEX
        xgl_route_index_node_t* node = &table->nodes[table->route_count];
        *node = (xgl_route_index_node_t){0};
        if (!xgl_route_index_insert(&table->index, node, target_id, route)) {
            return XGL_ERR_INVALID_PARAM;
        }
#endif
        ++table->route_count;
    }
    *route = (xgl_route_item_t){target_id, phy, max_frame_size, read_freq_hz,
                                metric};
    return XGL_OK;
}

/**
 * \brief           Remove a route and rebuild pointers after compaction
 */
xgl_error_t xgl_route_table_remove(xgl_route_table_t* table,
                                   uint16_t target_id) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    xgl_route_item_t* route = xgl_route_table_lookup(table, target_id);
    if (route == NULL) {
        return XGL_ERR_ROUTE_NOT_FOUND;
    }
    const size_t position = (size_t)(route - table->routes);
#if XGL_FEATURE_ROUTE_INDEX
    xgl_route_index_clear(&table->index);
#endif
    --table->route_count;
    memmove(route, route + 1U,
            (table->route_count - position) * sizeof(*route));
#if XGL_FEATURE_ROUTE_INDEX
    for (size_t i = 0U; i < table->route_count; ++i) {
        table->nodes[i] = (xgl_route_index_node_t){0};
        (void)xgl_route_index_insert(&table->index, &table->nodes[i],
                                     table->routes[i].target_id,
                                     &table->routes[i]);
    }
#endif
    return XGL_OK;
}

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Change the metric of an existing destination
 */
xgl_error_t xgl_route_table_update_metric(const xgl_route_table_t* table,
                                          uint16_t target_id, uint8_t metric) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
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

/**
 * \brief           Clear routes while retaining their reserved storage
 */
void xgl_route_table_clear(xgl_route_table_t* table) {
    if (table != NULL) {
#if XGL_FEATURE_ROUTE_INDEX
        xgl_route_index_clear(&table->index);
#endif
        table->route_count = 0U;
    }
}

/**
 * \brief           Validate a route set before replacing current entries
 */
xgl_error_t xgl_route_table_load(xgl_route_table_t* table,
                                 const xgl_route_item_t* routes, size_t count) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    if (routes == NULL && count != 0U) {
        return XGL_ERR_INVALID_PARAM;
    }
    if (count > table->route_capacity) {
        return XGL_ERR_QUEUE_FULL;
    }
    for (size_t i = 0U; i < count; ++i) {
        if (routes[i].phy == NULL || routes[i].target_id == 0U) {
            return XGL_ERR_INVALID_PARAM;
        }
        for (size_t j = 0U; j < i; ++j) {
            if (routes[j].target_id == routes[i].target_id) {
                return XGL_ERR_INVALID_PARAM;
            }
        }
    }
    xgl_route_table_clear(table);
    if (count != 0U) {
        /* Loading an owned subrange is valid; preserve overlapping storage. */
        memmove(table->routes, routes, count * sizeof(*routes));
    }
#if XGL_FEATURE_ROUTE_INDEX
    for (size_t i = 0U; i < count; ++i) {
        table->nodes[i] = (xgl_route_index_node_t){0};
        if (!xgl_route_index_insert(&table->index, &table->nodes[i],
                                    table->routes[i].target_id,
                                    &table->routes[i])) {
            xgl_route_table_clear(table);
            return XGL_ERR_INVALID_PARAM;
        }
    }
#endif
    table->route_count = count;
    return XGL_OK;
}
