/**
 * \file            xgl_allocator.c
 * \brief           Custom allocator support implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_allocator.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Default Allocator Implementation                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default malloc wrapper
 */
#if XGL_ALLOW_FALLBACK_MALLOC
static void* default_malloc(size_t size) {
    return malloc(size);
}

/**
 * \brief           Default free wrapper
 */
static void default_free(void* ptr) {
    free(ptr);
}

/**
 * \brief           Default allocator instance
 */
static xgl_allocator_t default_allocator = {
    .malloc = default_malloc,
    .free = default_free,
    .user_data = NULL
};
#endif

static void* tracking_malloc(size_t size);
static void tracking_free(void* ptr);
static void* tracking_malloc_impl(xgl_tracking_allocator_t* tracker, size_t size);
static void tracking_free_impl(xgl_tracking_allocator_t* tracker, void* ptr);

/**
 * \brief           Get default allocator
 */
xgl_allocator_t* xgl_allocator_get_default(void) {
#if XGL_ALLOW_FALLBACK_MALLOC
    return &default_allocator;
#else
    return NULL;
#endif
}

/*---------------------------------------------------------------------------*/
/* Allocator Wrapper Functions                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using provided allocator
 * \details         If allocator is NULL, uses default malloc/free only when
 *                  XGL_ALLOW_FALLBACK_MALLOC is enabled.
 */
void* xgl_alloc(xgl_allocator_t* allocator, size_t size) {
    /* Validate size */
    if (size == 0) {
        return NULL;
    }
    
    /* Use default allocator if none provided */
    if (allocator == NULL) {
#if XGL_ALLOW_FALLBACK_MALLOC
        allocator = &default_allocator;
#else
        return NULL;
#endif
    }
    
    /* Validate allocator has malloc function */
    if (allocator->malloc == NULL) {
        return NULL;
    }

    if (allocator->malloc == tracking_malloc && allocator->user_data != NULL) {
        return tracking_malloc_impl((xgl_tracking_allocator_t*)allocator->user_data, size);
    }
    
    /* Allocate memory */
    return allocator->malloc(size);
}

/**
 * \brief           Free memory using provided allocator
 * \details         If allocator is NULL, uses default malloc/free only when
 *                  XGL_ALLOW_FALLBACK_MALLOC is enabled.
 */
void xgl_free(xgl_allocator_t* allocator, void* ptr) {
    /* Nothing to free */
    if (ptr == NULL) {
        return;
    }
    
    /* Use default allocator if none provided */
    if (allocator == NULL) {
#if XGL_ALLOW_FALLBACK_MALLOC
        allocator = &default_allocator;
#else
        return;
#endif
    }
    
    /* Validate allocator has free function */
    if (allocator->free == NULL) {
        return;
    }

    if (allocator->free == tracking_free && allocator->user_data != NULL) {
        tracking_free_impl((xgl_tracking_allocator_t*)allocator->user_data, ptr);
        return;
    }
    
    /* Free memory */
    allocator->free(ptr);
}

/*---------------------------------------------------------------------------*/
/* Memory Tracking Allocator                                                 */
/*---------------------------------------------------------------------------*/

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

/**
 * \brief           Active tracker for allocator callback compatibility
 * \details         xgl_allocator_t callbacks do not receive user_data, so the
 *                  tracking allocator uses a single active tracker for the
 *                  callback interface. Direct xgl_tracking_alloc/free calls do
 *                  not depend on this global pointer.
 */
static xgl_tracking_allocator_t* active_tracker = NULL;

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

/**
 * \brief           Tracking allocator malloc implementation
 */
static void* tracking_malloc_impl(xgl_tracking_allocator_t* tracker, size_t size) {
    xgl_alloc_header_t* header;
    void* ptr;
    size_t total_size;
    
    if (tracker == NULL || tracker->underlying == NULL || size == 0) {
        return NULL;
    }
    
    /* Allocate extra space for header */
    total_size = size + sizeof(xgl_alloc_header_t);
    
    /* Allocate from underlying allocator */
    ptr = xgl_alloc(tracker->underlying, total_size);
    if (ptr == NULL) {
        return NULL;
    }
    
    /* Fill in header */
    header = (xgl_alloc_header_t*)ptr;
    header->size = size;
    header->magic = XGL_ALLOC_MAGIC;
    header->phase = tracker->current_phase;
    
    /* Update statistics */
    update_alloc_stats(&tracker->stats, size);
    update_alloc_stats(&tracker->phase_stats.phase[tracker->current_phase], size);
    
    /* Return pointer after header */
    return (uint8_t*)ptr + sizeof(xgl_alloc_header_t);
}

/**
 * \brief           Tracking allocator malloc wrapper
 */
static void* tracking_malloc(size_t size) {
    return tracking_malloc_impl(active_tracker, size);
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
    
    /* Get header */
    actual_ptr = (uint8_t*)ptr - sizeof(xgl_alloc_header_t);
    header = (xgl_alloc_header_t*)actual_ptr;
    
    /* Validate magic number */
    if (header->magic != XGL_ALLOC_MAGIC) {
        /* Invalid pointer or corruption */
        return;
    }
    
    /* Update statistics */
    update_free_stats(&tracker->stats, header->size);
    if (header->phase < XGL_ALLOCATOR_PHASE_COUNT) {
        update_free_stats(&tracker->phase_stats.phase[header->phase], header->size);
    }
    
    /* Clear magic to detect double-free */
    header->magic = 0;
    
    /* Free from underlying allocator */
    xgl_free(tracker->underlying, actual_ptr);
}

/**
 * \brief           Tracking allocator free wrapper
 */
static void tracking_free(void* ptr) {
    tracking_free_impl(active_tracker, ptr);
}

/**
 * \brief           Initialize tracking allocator
 * \details         Sets up tracking wrapper around underlying allocator
 */
int xgl_tracking_allocator_init(xgl_tracking_allocator_t* tracker,
                                xgl_allocator_t* underlying) {
    /* Validate parameter */
    if (tracker == NULL) {
        return -1;
    }
    
    if (underlying == NULL) {
#if XGL_ALLOW_FALLBACK_MALLOC
        underlying = &default_allocator;
#else
        return -1;
#endif
    }
    
    /* Initialize tracker structure */
    memset(tracker, 0, sizeof(xgl_tracking_allocator_t));
    tracker->underlying = underlying;
    tracker->current_phase = XGL_ALLOCATOR_PHASE_INIT;
    
    /* Set up base allocator interface */
    /* Note: This simplified implementation doesn't support proper context */
    /* A production implementation would use thread-local storage */
    tracker->base.malloc = tracking_malloc;
    tracker->base.free = tracking_free;
    tracker->base.user_data = tracker;
    active_tracker = tracker;
    
    return 0;
}

/**
 * \brief           Get tracking allocator statistics
 */
void xgl_tracking_allocator_get_stats(const xgl_tracking_allocator_t* tracker,
                                     xgl_allocator_stats_t* stats) {
    /* Validate parameters */
    if (tracker == NULL || stats == NULL) {
        return;
    }
    
    /* Copy statistics */
    *stats = tracker->stats;
}

/**
 * \brief           Reset tracking allocator statistics
 */
void xgl_tracking_allocator_reset_stats(xgl_tracking_allocator_t* tracker) {
    /* Validate parameter */
    if (tracker == NULL) {
        return;
    }
    
    /* Reset statistics (keep current_allocated) */
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
