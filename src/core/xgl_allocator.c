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

/**
 * \brief           Get default allocator
 */
xgl_allocator_t* xgl_allocator_get_default(void) {
    return &default_allocator;
}

/*---------------------------------------------------------------------------*/
/* Allocator Wrapper Functions                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using provided allocator
 * \details         If allocator is NULL, uses default malloc/free
 */
void* xgl_alloc(xgl_allocator_t* allocator, size_t size) {
    /* Validate size */
    if (size == 0) {
        return NULL;
    }
    
    /* Use default allocator if none provided */
    if (allocator == NULL) {
        allocator = &default_allocator;
    }
    
    /* Validate allocator has malloc function */
    if (allocator->malloc == NULL) {
        return NULL;
    }
    
    /* Allocate memory */
    return allocator->malloc(size);
}

/**
 * \brief           Free memory using provided allocator
 * \details         If allocator is NULL, uses default malloc/free
 */
void xgl_free(xgl_allocator_t* allocator, void* ptr) {
    /* Nothing to free */
    if (ptr == NULL) {
        return;
    }
    
    /* Use default allocator if none provided */
    if (allocator == NULL) {
        allocator = &default_allocator;
    }
    
    /* Validate allocator has free function */
    if (allocator->free == NULL) {
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
} xgl_alloc_header_t;

/**
 * \brief           Magic number for allocation validation
 */
#define XGL_ALLOC_MAGIC             0xA110CA7E

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
    
    /* Update statistics */
    tracker->stats.total_allocated += size;
    tracker->stats.current_allocated += size;
    tracker->stats.alloc_count++;
    
    /* Update peak */
    if (tracker->stats.current_allocated > tracker->stats.peak_allocated) {
        tracker->stats.peak_allocated = tracker->stats.current_allocated;
    }
    
    /* Return pointer after header */
    return (uint8_t*)ptr + sizeof(xgl_alloc_header_t);
}

/**
 * \brief           Tracking allocator malloc wrapper
 */
static void* tracking_malloc(size_t size) {
    /* This is a wrapper that extracts tracker from user_data */
    /* Note: This requires the allocator to be properly initialized */
    /* with user_data pointing to the tracker instance */
    (void)size;  /* Unused parameter */
    return NULL;  /* Cannot be used directly - use tracking_malloc_impl instead */
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
    tracker->stats.total_freed += header->size;
    tracker->stats.current_allocated -= header->size;
    tracker->stats.free_count++;
    
    /* Clear magic to detect double-free */
    header->magic = 0;
    
    /* Free from underlying allocator */
    xgl_free(tracker->underlying, actual_ptr);
}

/**
 * \brief           Tracking allocator free wrapper
 */
static void tracking_free(void* ptr) {
    /* This is a wrapper - cannot be used directly */
    /* Use tracking_free_impl instead */
    (void)ptr;
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
    
    /* Use default allocator if none provided */
    if (underlying == NULL) {
        underlying = &default_allocator;
    }
    
    /* Initialize tracker structure */
    memset(tracker, 0, sizeof(xgl_tracking_allocator_t));
    tracker->underlying = underlying;
    
    /* Set up base allocator interface */
    /* Note: This simplified implementation doesn't support proper context */
    /* A production implementation would use thread-local storage */
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
