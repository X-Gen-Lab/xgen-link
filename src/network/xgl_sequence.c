/**
 * \file            xgl_sequence.c
 * \brief           Sequence number management implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_sequence.h>
#include <xgl/xgl_error.h>
#include <string.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
/* Private Helper Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using context's allocator
 */
static void* xgl_sequence_alloc(xgl_sequence_ctx_t* ctx, size_t size) {
    if (ctx->allocator && ctx->allocator->malloc) {
        return ctx->allocator->malloc(size);
    }
    return malloc(size);
}

/**
 * \brief           Free memory using context's allocator
 */
static void xgl_sequence_free(xgl_sequence_ctx_t* ctx, void* ptr) {
    if (ctx->allocator && ctx->allocator->free) {
        ctx->allocator->free(ptr);
    } else {
        free(ptr);
    }
}

/**
 * \brief           Validate target ID is within range
 */
static inline bool xgl_sequence_validate_target_id(const xgl_sequence_ctx_t* ctx,
                                                   uint8_t target_id) {
    return (target_id < ctx->max_routes);
}

/*---------------------------------------------------------------------------*/
/* Public API Implementation                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize sequence number context
 */
xgl_error_t xgl_sequence_init(xgl_sequence_ctx_t* ctx,
                              size_t max_routes,
                              xgl_allocator_t* allocator) {
    if (ctx == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (max_routes == 0 || max_routes > XGL_SEQ_MAX_ROUTES) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Initialize context */
    memset(ctx, 0, sizeof(xgl_sequence_ctx_t));
    ctx->max_routes = max_routes;
    ctx->allocator = allocator;
    
    /* Allocate sequence number array */
    ctx->seq_numbers = (uint8_t*)xgl_sequence_alloc(ctx, max_routes);
    if (ctx->seq_numbers == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    
    /* Initialize all sequence numbers to initial value */
    memset(ctx->seq_numbers, XGL_SEQ_NUM_INITIAL, max_routes);
    
    return XGL_OK;
}

/**
 * \brief           Destroy sequence number context
 */
void xgl_sequence_destroy(xgl_sequence_ctx_t* ctx) {
    if (ctx == NULL) {
        return;
    }
    
    /* Free sequence number array */
    if (ctx->seq_numbers != NULL) {
        xgl_sequence_free(ctx, ctx->seq_numbers);
        ctx->seq_numbers = NULL;
    }
    
    /* Reset context */
    ctx->max_routes = 0;
}

/**
 * \brief           Get next sequence number for a route
 * \details         Increments and returns the sequence number for the specified route
 */
xgl_error_t xgl_sequence_get_next(xgl_sequence_ctx_t* ctx,
                                  uint8_t target_id,
                                  uint8_t* seq_num) {
    if (ctx == NULL || seq_num == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (ctx->seq_numbers == NULL) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
    if (!xgl_sequence_validate_target_id(ctx, target_id)) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Get current sequence number */
    *seq_num = ctx->seq_numbers[target_id];
    
    /* Increment for next use (handles wraparound automatically) */
    ctx->seq_numbers[target_id] = xgl_sequence_increment(*seq_num);
    
    return XGL_OK;
}

/**
 * \brief           Get current sequence number for a route (without incrementing)
 */
xgl_error_t xgl_sequence_get_current(const xgl_sequence_ctx_t* ctx,
                                     uint8_t target_id,
                                     uint8_t* seq_num) {
    if (ctx == NULL || seq_num == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (ctx->seq_numbers == NULL) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
    if (!xgl_sequence_validate_target_id(ctx, target_id)) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Return current sequence number without incrementing */
    *seq_num = ctx->seq_numbers[target_id];
    
    return XGL_OK;
}

/**
 * \brief           Reset sequence number for a route
 */
xgl_error_t xgl_sequence_reset(xgl_sequence_ctx_t* ctx,
                               uint8_t target_id) {
    if (ctx == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (ctx->seq_numbers == NULL) {
        return XGL_ERR_NOT_INITIALIZED;
    }
    
    if (!xgl_sequence_validate_target_id(ctx, target_id)) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Reset sequence number to initial value */
    ctx->seq_numbers[target_id] = XGL_SEQ_NUM_INITIAL;
    
    return XGL_OK;
}

/**
 * \brief           Reset all sequence numbers
 */
void xgl_sequence_reset_all(xgl_sequence_ctx_t* ctx) {
    if (ctx == NULL || ctx->seq_numbers == NULL) {
        return;
    }
    
    /* Reset all sequence numbers to initial value */
    memset(ctx->seq_numbers, XGL_SEQ_NUM_INITIAL, ctx->max_routes);
}

/**
 * \brief           Check if sequence number is valid (within expected range)
 * \details         Validates received sequence number against expected value
 *                  considering sliding window and wraparound
 */
bool xgl_sequence_is_valid(uint8_t expected,
                           uint8_t received,
                           uint8_t window_size) {
    /* Calculate difference with wraparound handling */
    int16_t diff = xgl_sequence_diff(received, expected);
    
    /* Valid if within window: [expected, expected + window_size) */
    /* Also accept slightly old packets: [expected - window_size, expected) */
    return (diff >= -(int16_t)window_size && diff < (int16_t)window_size);
}

/**
 * \brief           Calculate sequence number difference (handles wraparound)
 * \details         Computes (seq1 - seq2) with proper wraparound handling
 */
int16_t xgl_sequence_diff(uint8_t seq1, uint8_t seq2) {
    int16_t diff = (int16_t)seq1 - (int16_t)seq2;
    
    /* Handle wraparound using circular arithmetic */
    /* If difference is > 128, it means seq1 wrapped around and is actually less */
    if (diff > 128) {
        diff -= 256;
    } else if (diff < -128) {
        diff += 256;
    }
    
    return diff;
}
