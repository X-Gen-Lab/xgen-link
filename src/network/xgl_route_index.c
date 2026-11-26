/**
 * \file            xgl_route_index.c
 * \brief           Optional fixed-bucket index for network routes
 * \author          X-Gen Lab
 */

#include <network/xgl_route_index.h>

static size_t route_bucket(const xgl_route_index_t* index, uint16_t target_id) {
    /* Keep the distribution of the previous index for 16-bit route keys. */
    uint32_t key = (uint32_t)target_id * UINT32_C(0x7feb352d);
    key ^= key >> 15U;
    return (size_t)key & (index->bucket_count - 1U);
}

bool xgl_route_index_init(xgl_route_index_t* index,
                          xgl_route_index_node_t** buckets, size_t bucket_count,
                          size_t capacity) {
    if (index == NULL || buckets == NULL || bucket_count == 0U ||
        (bucket_count & (bucket_count - 1U)) != 0U || capacity == 0U ||
        bucket_count > SIZE_MAX / sizeof(*buckets)) {
        return false;
    }

    for (size_t i = 0U; i < bucket_count; ++i) {
        buckets[i] = NULL;
    }
    *index = (xgl_route_index_t){buckets, bucket_count, capacity, 0U};
    return true;
}

xgl_route_item_t* xgl_route_index_find(const xgl_route_index_t* index,
                                       uint16_t target_id) {
    if (index == NULL || index->buckets == NULL || index->bucket_count == 0U) {
        return NULL;
    }

    const xgl_route_index_node_t* node =
        index->buckets[route_bucket(index, target_id)];
    while (node != NULL) {
        if (node->target_id == target_id) {
            return node->route;
        }
        node = node->next;
    }
    return NULL;
}

bool xgl_route_index_insert(xgl_route_index_t* index,
                            xgl_route_index_node_t* node, uint16_t target_id,
                            xgl_route_item_t* route) {
    if (index == NULL || index->buckets == NULL || index->bucket_count == 0U ||
        node == NULL || route == NULL || node->owner != NULL ||
        index->count >= index->capacity ||
        xgl_route_index_find(index, target_id) != NULL) {
        return false;
    }

    const size_t bucket = route_bucket(index, target_id);
    *node = (xgl_route_index_node_t){target_id, route, index->buckets[bucket],
                                     index};
    index->buckets[bucket] = node;
    ++index->count;
    return true;
}

void xgl_route_index_clear(xgl_route_index_t* index) {
    if (index == NULL || index->buckets == NULL) {
        return;
    }

    for (size_t i = 0U; i < index->bucket_count; ++i) {
        xgl_route_index_node_t* node = index->buckets[i];
        while (node != NULL) {
            xgl_route_index_node_t* next = node->next;
            node->next = NULL;
            node->owner = NULL;
            node = next;
        }
        index->buckets[i] = NULL;
    }
    index->count = 0U;
}
