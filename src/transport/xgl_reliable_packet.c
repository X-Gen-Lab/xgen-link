/**
 * \file            xgl_reliable_packet.c
 * \brief           Reliable queue packet storage helpers
 * \author          X-Gen Lab
 */

#include "xgl_reliable_internal.h"
#include <xgl/internal/xgl_allocator.h>

void* reliable_malloc(xgl_allocator_t* allocator, size_t size) {
    return xgl_alloc(allocator, size);
}

void reliable_free(xgl_allocator_t* allocator, void* ptr) {
    xgl_free(allocator, ptr);
}

static size_t reliable_index_bucket(uint16_t target_id,
                                    uint32_t packet_number) {
    uint32_t mixed = packet_number ^ ((uint32_t)target_id * 2654435761UL);
    return (size_t)(mixed % XGL_RELIABLE_INDEX_BUCKETS);
}

void reliable_index_packet(xgl_reliable_queue_t* queue,
                           xgl_reliable_packet_t* packet) {
    if (queue == NULL || packet == NULL) {
        return;
    }

    size_t bucket = reliable_index_bucket(packet->target_id,
                                          packet->packet_number);
    packet->index_next = queue->index_buckets[bucket];
    queue->index_buckets[bucket] = packet;
}

void reliable_unindex_packet(xgl_reliable_queue_t* queue,
                             xgl_reliable_packet_t* packet) {
    if (queue == NULL || packet == NULL) {
        return;
    }

    size_t bucket = reliable_index_bucket(packet->target_id,
                                          packet->packet_number);
    xgl_reliable_packet_t* previous = NULL;
    xgl_reliable_packet_t* current = queue->index_buckets[bucket];
    while (current != NULL) {
        if (current == packet) {
            if (previous == NULL) {
                queue->index_buckets[bucket] = current->index_next;
            } else {
                previous->index_next = current->index_next;
            }
            current->index_next = NULL;
            return;
        }
        previous = current;
        current = current->index_next;
    }
}

void reliable_free_packet(xgl_reliable_queue_t* queue,
                          xgl_reliable_packet_t* packet) {
    if (packet == NULL) {
        return;
    }

    if (packet->data != NULL) {
        reliable_free(queue->allocator, packet->data);
        packet->data = NULL;
    }

    if (packet->extensions != NULL) {
        reliable_free(queue->allocator, packet->extensions);
        packet->extensions = NULL;
    }

    reliable_free(queue->allocator, packet);
}

xgl_reliable_packet_t* xgl_reliable_find_packet_number(const xgl_reliable_queue_t* queue,
                                                       uint32_t packet_number,
                                                       uint16_t target_id) {
    if (queue == NULL) {
        return NULL;
    }

    size_t bucket = reliable_index_bucket(target_id, packet_number);
    xgl_reliable_packet_t* packet = queue->index_buckets[bucket];
    while (packet != NULL) {
        if (packet->packet_number == packet_number && packet->target_id == target_id) {
            return packet;
        }
        packet = packet->index_next;
    }

    return NULL;
}
