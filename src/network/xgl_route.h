/**
 * \file            xgl_route.h
 * \brief           Route table management for network layer
 * \author          X-Gen Lab
 */

#ifndef XGL_ROUTE_H
#define XGL_ROUTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "xgl/xgl_build_config.h"
#include "xgl/xgl_error.h"
#include "xgl/xgl_types.h"

#if XGL_FEATURE_ROUTE_INDEX
#include <network/xgl_route_index.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Route Table Configuration                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default route table size (must be power of 2)
 */
#define XGL_ROUTE_TABLE_DEFAULT_SIZE 16

/**
 * \brief           Maximum reserved route metadata value
 */
#define XGL_ROUTE_METRIC_MAX 255

/**
 * \brief           Default reserved route metadata value
 */
#define XGL_ROUTE_METRIC_DEFAULT 100

/*---------------------------------------------------------------------------*/
/* Route Table Structure                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Route table structure
 */
typedef struct {
#if XGL_FEATURE_ROUTE_INDEX
    xgl_route_index_t index;       /**< Caller-sized route index */
    xgl_route_index_node_t* nodes; /**< One node per reserved route */
#endif
    xgl_route_item_t* routes;         /**< Array of route items */
    size_t route_count;               /**< Number of routes */
    size_t route_capacity;            /**< Capacity of routes array */
    const xgl_allocator_t* allocator; /**< Memory allocator */
} xgl_route_table_t;

/*---------------------------------------------------------------------------*/
/* Route Table API                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize route table
 * \param[in,out]   table: Route table structure
 * \param[in]       initial_capacity: Fixed route capacity; additions never grow
 * it
 * \param[in]       allocator: Explicit memory allocator
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_route_table_init(xgl_route_table_t* table,
                                 size_t initial_capacity,
                                 const xgl_allocator_t* allocator);

/**
 * \brief           Destroy route table and free resources
 * \param[in]       table: Route table structure
 */
void xgl_route_table_destroy(xgl_route_table_t* table);

/**
 * \brief           Add route to table
 * \param[in,out]   table: Route table structure
 * \param[in]       target_id: Target node ID
 * \param[in]       phy: Physical layer operations
 * \param[in]       max_frame_size: Maximum frame size for this route
 * \param[in]       read_freq_hz: Retained metadata; does not reschedule PHY RX
 * \param[in]       metric: Reserved metadata; does not affect route selection
 * \return          XGL_OK on success, error code otherwise
 * \note            An existing destination is updated even at full capacity.
 *                  Runtime changes do not register or replace receive links.
 */
xgl_error_t xgl_route_table_add(xgl_route_table_t* table, uint16_t target_id,
                                xgl_phy_ops_t* phy, uint16_t max_frame_size,
                                uint32_t read_freq_hz, uint8_t metric);

/**
 * \brief           Remove route from table
 * \param[in,out]   table: Route table structure
 * \param[in]       target_id: Target node ID
 * \return          XGL_OK on success, XGL_ERR_ROUTE_NOT_FOUND if not found
 */
xgl_error_t xgl_route_table_remove(xgl_route_table_t* table,
                                   uint16_t target_id);

/**
 * \brief           Lookup a route using the configured index strategy
 * \param[in]       table: Route table metadata; owned entries remain mutable
 * \param[in]       target_id: Target node ID
 * \return          Borrowed mutable route, or NULL if not found
 * \note            Adding a route does not move storage. Removing, clearing,
 *                  loading or destroying the table invalidates prior views.
 *                  Updating a destination replaces its contents in place.
 *                  The caller serializes reads and updates.
 */
xgl_route_item_t* xgl_route_table_lookup(const xgl_route_table_t* table,
                                         uint16_t target_id);

/**
 * \brief           Update retained metric metadata without changing selection
 * \param[in,out]   table: Route table structure
 * \param[in]       target_id: Target node ID
 * \param[in]       metric: New metric value
 * \return          XGL_OK on success, XGL_ERR_ROUTE_NOT_FOUND if not found
 */
xgl_error_t xgl_route_table_update_metric(const xgl_route_table_t* table,
                                          uint16_t target_id, uint8_t metric);

/**
 * \brief           Clear all routes from table
 * \param[in,out]   table: Route table structure
 */
void xgl_route_table_clear(xgl_route_table_t* table);

/**
 * \brief           Get number of routes in table
 * \param[in]       table: Route table structure
 * \return          Number of routes
 */
static inline size_t xgl_route_table_count(const xgl_route_table_t* table) {
    return table ? table->route_count : 0;
}

/**
 * \brief           Check if route table is empty
 * \param[in]       table: Route table structure
 * \return          true if empty, false otherwise
 */
static inline bool xgl_route_table_is_empty(const xgl_route_table_t* table) {
    return table ? (table->route_count == 0) : true;
}

/**
 * \brief           Replace routes after validating the complete configuration
 * \param[in,out]   table: Route table structure
 * \param[in]       routes: Array of route items
 * \param[in]       count: Number of routes
 * \return          XGL_OK on success, error code otherwise
 * \note            Zero destinations, duplicate destinations and NULL PHYs
 *                  are rejected before existing routes are changed. Broadcast
 *                  is permitted; instance configuration rejects local targets.
 */
xgl_error_t xgl_route_table_load(xgl_route_table_t* table,
                                 const xgl_route_item_t* routes, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* XGL_ROUTE_H */
