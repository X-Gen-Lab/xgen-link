/**
 * \file            xgl_route.h
 * \brief           Route table management for network layer
 * \author          Nexus Team
 */

#ifndef XGL_ROUTE_H
#define XGL_ROUTE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_types.h"
#include "xgl_error.h"
#include "xgl_hashtable.h"

/*---------------------------------------------------------------------------*/
/* Route Table Configuration                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default route table size (must be power of 2)
 */
#define XGL_ROUTE_TABLE_DEFAULT_SIZE    16

/**
 * \brief           Maximum route metric value
 */
#define XGL_ROUTE_METRIC_MAX            255

/**
 * \brief           Default route metric
 */
#define XGL_ROUTE_METRIC_DEFAULT        100

/*---------------------------------------------------------------------------*/
/* Route Table Structure                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Route table structure
 */
typedef struct {
    xgl_hashtable_t hashtable;      /**< Hash table for O(1) lookup */
    xgl_route_item_t* routes;       /**< Array of route items */
    size_t route_count;             /**< Number of routes */
    size_t route_capacity;          /**< Capacity of routes array */
    xgl_allocator_t* allocator;     /**< Memory allocator */
} xgl_route_table_t;

/*---------------------------------------------------------------------------*/
/* Route Table API                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize route table
 * \param[in,out]   table: Route table structure
 * \param[in]       initial_capacity: Initial capacity for routes
 * \param[in]       allocator: Memory allocator (NULL = malloc/free)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_route_table_init(xgl_route_table_t* table,
                                 size_t initial_capacity,
                                 xgl_allocator_t* allocator);

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
 * \param[in]       read_freq_hz: Read frequency in Hz
 * \param[in]       metric: Route metric (lower is better)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_route_table_add(xgl_route_table_t* table,
                                uint8_t target_id,
                                xgl_phy_ops_t* phy,
                                uint16_t max_frame_size,
                                uint32_t read_freq_hz,
                                uint8_t metric);

/**
 * \brief           Remove route from table
 * \param[in,out]   table: Route table structure
 * \param[in]       target_id: Target node ID
 * \return          XGL_OK on success, XGL_ERR_ROUTE_NOT_FOUND if not found
 */
xgl_error_t xgl_route_table_remove(xgl_route_table_t* table,
                                   uint8_t target_id);

/**
 * \brief           Lookup route in table (O(1) average)
 * \param[in]       table: Route table structure
 * \param[in]       target_id: Target node ID
 * \return          Route item pointer, NULL if not found
 */
xgl_route_item_t* xgl_route_table_lookup(const xgl_route_table_t* table,
                                         uint8_t target_id);

/**
 * \brief           Update route metric
 * \param[in,out]   table: Route table structure
 * \param[in]       target_id: Target node ID
 * \param[in]       metric: New metric value
 * \return          XGL_OK on success, XGL_ERR_ROUTE_NOT_FOUND if not found
 */
xgl_error_t xgl_route_table_update_metric(xgl_route_table_t* table,
                                          uint8_t target_id,
                                          uint8_t metric);

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
 * \brief           Load routes from configuration
 * \param[in,out]   table: Route table structure
 * \param[in]       routes: Array of route items
 * \param[in]       count: Number of routes
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_route_table_load(xgl_route_table_t* table,
                                 const xgl_route_item_t* routes,
                                 size_t count);

#ifdef __cplusplus
}
#endif

#endif /* XGL_ROUTE_H */
