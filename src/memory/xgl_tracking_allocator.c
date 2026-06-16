/**
 * \file            xgl_tracking_allocator.c
 * \brief           Memory tracking allocator implementation
 * \author          X-Gen Lab
 */

#include "xgl_allocator_internal.h"
#include <stdint.h>
#include <string.h>

/**
 * \brief           Allocation header for tracking
 */
typedef struct {
    size_t size;                    /**< Size of allocation */
    uint32_t magic;                 /**< Magic number for validation */
    xgl_allocator_phase_t phase;    /**< Accounting phase */
} xgl_alloc_header_t;

/**
 * \brief           Magic number for allocation validation
 */
#define XGL_ALLOC_MAGIC             0xA110CA7E

static void* tracking_malloc(size_t size);
static void tracking_free(void* ptr);
static void* tracking_malloc_impl(xgl_tracking_allocator_t* tracker,
                                  size_t size);
static void tracking_free_impl(xgl_tracking_allocator_t* tracker, void* ptr);

static void update_alloc_stats(xgl_allocator_stats_t* stats, size_t size) {
    if (stats == NULL) {
        return;
    }

    stats->total_allocated += size;
    stats->current_allocated += size;
    stats->alloc_count++;

    if (stats->current_allocated > stats->peak_allocated) {
        stats->peak_allocated = stats->current_allocated;
    }
}

static void update_free_stats(xgl_allocator_stats_t* stats, size_t size) {
    if (stats == NULL) {
        return;
    }

    stats->total_freed += size;
    if (stats->current_allocated >= size) {
        stats->current_allocated -= size;
    } else {
        stats->current_allocated = 0;
    }
    stats->free_count++;
}

bool xgl_tracking_allocator_is_alloc_callback(
    const xgl_allocator_t* allocator) {
    return allocator != NULL &&
           allocator->malloc == tracking_malloc &&
           allocator->user_data != NULL;
}

bool xgl_tracking_allocator_is_free_callback(
    const xgl_allocator_t* allocator) {
    return allocator != NULL &&
           allocator->free == tracking_free &&
           allocator->user_data != NULL;
}

void* xgl_tracking_allocator_alloc_from_interface(
    xgl_allocator_t* allocator,
    size_t size) {
    if (!xgl_tracking_allocator_is_alloc_callback(allocator)) {
        return NULL;
    }

    return tracking_malloc_impl(
        (xgl_tracking_allocator_t*)allocator->user_data,
        size);
}

void xgl_tracking_allocator_free_from_interface(
    xgl_allocator_t* allocator,
    void* ptr) {
    if (!xgl_tracking_allocator_is_free_callback(allocator)) {
        return;
    }

    tracking_free_impl((xgl_tracking_allocator_t*)allocator->user_data, ptr);
}

/**
 * \brief           Tracking allocator malloc implementation
 */
static void* tracking_malloc_impl(xgl_tracking_allocator_t* tracker,
                                  size_t size) {
    xgl_alloc_header_t* header;
    void* ptr;
    size_t total_size;

    if (tracker == NULL || tracker->underlying == NULL || size == 0) {
        return NULL;
    }

    total_size = size + sizeof(xgl_alloc_header_t);

    ptr = xgl_alloc(tracker->underlying, total_size);
    if (ptr == NULL) {
        return NULL;
    }

    header = (xgl_alloc_header_t*)ptr;
    header->size = size;
    header->magic = XGL_ALLOC_MAGIC;
    header->phase = tracker->current_phase;

    update_alloc_stats(&tracker->stats, size);
    update_alloc_stats(&tracker->phase_stats.phase[tracker->current_phase],
                       size);

    return (uint8_t*)ptr + sizeof(xgl_alloc_header_t);
}

/**
 * \brief           Tracking allocator malloc wrapper
 * \details         xgl_allocator_t callbacks do not receive user_data. Use
 *                  xgl_alloc(interface, size) or xgl_tracking_alloc() so the
 *                  tracker is resolved from user_data instead of a global.
 */
static void* tracking_malloc(size_t size) {
    (void)size;
    return NULL;
}

/**
 * \brief           Tracking allocator free implementation
 */
static void tracking_free_impl(xgl_tracking_allocator_t* tracker, void* ptr) {
    xgl_alloc_header_t* header;
    void* actual_ptr;

    if (ptr == NULL) {
        return;
    }

    if (tracker == NULL || tracker->underlying == NULL) {
        return;
    }

    actual_ptr = (uint8_t*)ptr - sizeof(xgl_alloc_header_t);
    header = (xgl_alloc_header_t*)actual_ptr;

    if (header->magic != XGL_ALLOC_MAGIC) {
        return;
    }

    update_free_stats(&tracker->stats, header->size);
    if (header->phase < XGL_ALLOCATOR_PHASE_COUNT) {
        update_free_stats(&tracker->phase_stats.phase[header->phase],
                          header->size);
    }

    header->magic = 0;

    xgl_free(tracker->underlying, actual_ptr);
}

/**
 * \brief           Tracking allocator free wrapper
 * \details         See tracking_malloc().
 */
static void tracking_free(void* ptr) {
    (void)ptr;
}

/**
 * \brief           Initialize tracking allocator
 * \details         Sets up tracking wrapper around underlying allocator
 */
int xgl_tracking_allocator_init(xgl_tracking_allocator_t* tracker,
                                xgl_allocator_t* underlying) {
    if (tracker == NULL) {
        return -1;
    }

    if (underlying == NULL) {
        underlying = xgl_allocator_get_default();
        if (underlying == NULL) {
            return -1;
        }
    }

    memset(tracker, 0, sizeof(xgl_tracking_allocator_t));
    tracker->underlying = underlying;
    tracker->current_phase = XGL_ALLOCATOR_PHASE_INIT;

    tracker->base.malloc = tracking_malloc;
    tracker->base.free = tracking_free;
    tracker->base.user_data = tracker;

    return 0;
}

/**
 * \brief           Get tracking allocator statistics
 */
void xgl_tracking_allocator_get_stats(const xgl_tracking_allocator_t* tracker,
                                      xgl_allocator_stats_t* stats) {
    if (tracker == NULL || stats == NULL) {
        return;
    }

    *stats = tracker->stats;
}

/**
 * \brief           Reset tracking allocator statistics
 */
void xgl_tracking_allocator_reset_stats(xgl_tracking_allocator_t* tracker) {
    if (tracker == NULL) {
        return;
    }

    tracker->stats.total_allocated = tracker->stats.current_allocated;
    tracker->stats.total_freed = 0;
    tracker->stats.peak_allocated = tracker->stats.current_allocated;
    tracker->stats.alloc_count = 0;
    tracker->stats.free_count = 0;

    for (size_t i = 0; i < (size_t)XGL_ALLOCATOR_PHASE_COUNT; i++) {
        xgl_allocator_stats_t* phase_stats = &tracker->phase_stats.phase[i];
        phase_stats->total_allocated = phase_stats->current_allocated;
        phase_stats->total_freed = 0;
        phase_stats->peak_allocated = phase_stats->current_allocated;
        phase_stats->alloc_count = 0;
        phase_stats->free_count = 0;
    }
}

/**
 * \brief           Set current tracking phase
 */
void xgl_tracking_allocator_set_phase(xgl_tracking_allocator_t* tracker,
                                      xgl_allocator_phase_t phase) {
    if (tracker == NULL || phase >= XGL_ALLOCATOR_PHASE_COUNT) {
        return;
    }

    tracker->current_phase = phase;
}

/**
 * \brief           Get per-phase tracking allocator statistics
 */
void xgl_tracking_allocator_get_phase_stats(
    const xgl_tracking_allocator_t* tracker,
    xgl_allocator_phase_stats_t* stats) {
    if (tracker == NULL || stats == NULL) {
        return;
    }

    *stats = tracker->phase_stats;
}

/**
 * \brief           Get base allocator interface from tracking allocator
 */
xgl_allocator_t* xgl_tracking_allocator_get_interface(
    xgl_tracking_allocator_t* tracker) {
    if (tracker == NULL) {
        return NULL;
    }

    return &tracker->base;
}

/**
 * \brief           Allocate memory using tracking allocator
 */
void* xgl_tracking_alloc(xgl_tracking_allocator_t* tracker, size_t size) {
    return tracking_malloc_impl(tracker, size);
}

/**
 * \brief           Free memory using tracking allocator
 */
void xgl_tracking_free(xgl_tracking_allocator_t* tracker, void* ptr) {
    tracking_free_impl(tracker, ptr);
}
