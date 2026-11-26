/**
 * \file            xgl_route_index.h
 * \brief           Fixed-capacity index over borrowed protocol routes
 * \author          X-Gen Lab
 */

#ifndef XGL_ROUTE_INDEX_H
#define XGL_ROUTE_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <xgl/xgl_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xgl_route_index xgl_route_index_t;

typedef struct xgl_route_index_node {
    uint16_t target_id;
    xgl_route_item_t* route;
    struct xgl_route_index_node* next;
    xgl_route_index_t* owner;
} xgl_route_index_node_t;

struct xgl_route_index {
    xgl_route_index_node_t** buckets;
    size_t bucket_count;
    size_t capacity;
    size_t count;
};

/* The route table owns bucket storage and nodes. Clear the index before moving
 * routes or nodes, and rebuild it after compaction.
 */
bool xgl_route_index_init(xgl_route_index_t* index,
                          xgl_route_index_node_t** buckets, size_t bucket_count,
                          size_t capacity);
bool xgl_route_index_insert(xgl_route_index_t* index,
                            xgl_route_index_node_t* node, uint16_t target_id,
                            xgl_route_item_t* route);
xgl_route_item_t* xgl_route_index_find(const xgl_route_index_t* index,
                                       uint16_t target_id);
void xgl_route_index_clear(xgl_route_index_t* index);

#ifdef __cplusplus
}
#endif

#endif /* XGL_ROUTE_INDEX_H */
