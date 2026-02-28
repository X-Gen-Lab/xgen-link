/**
 * \file            xgl_sequence.h
 * \brief           Sequence number management for network layer
 * \author          Nexus Team
 */

#ifndef XGL_SEQUENCE_H
#define XGL_SEQUENCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl_types.h"
#include "xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Sequence Number Configuration                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Maximum sequence number value (8-bit)
 */
#define XGL_SEQ_NUM_MAX             255

/**
 * \brief           Initial sequence number
 */
#define XGL_SEQ_NUM_INITIAL         0

/**
 * \brief           Default maximum number of routes for sequence tracking
 */
#define XGL_SEQ_MAX_ROUTES          256

/*---------------------------------------------------------------------------*/
/* Sequence Number Context                                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Sequence number context structure
 */
typedef struct {
    uint8_t* seq_numbers;           /**< Array of sequence numbers per route */
    size_t max_routes;              /**< Maximum number of routes */
    xgl_allocator_t* allocator;     /**< Memory allocator */
} xgl_sequence_ctx_t;

/*---------------------------------------------------------------------------*/
/* Sequence Number API                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize sequence number context
 * \param[in,out]   ctx: Sequence number context
 * \param[in]       max_routes: Maximum number of routes to track
 * \param[in]       allocator: Memory allocator (NULL = malloc/free)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_sequence_init(xgl_sequence_ctx_t* ctx,
                              size_t max_routes,
                              xgl_allocator_t* allocator);

/**
 * \brief           Destroy sequence number context
 * \param[in]       ctx: Sequence number context
 */
void xgl_sequence_destroy(xgl_sequence_ctx_t* ctx);

/**
 * \brief           Get next sequence number for a route
 * \param[in,out]   ctx: Sequence number context
 * \param[in]       target_id: Target node ID (route identifier)
 * \param[out]      seq_num: Pointer to store sequence number
 * \return          XGL_OK on success, error code otherwise
 * \note            This function increments the sequence number for the route
 */
xgl_error_t xgl_sequence_get_next(xgl_sequence_ctx_t* ctx,
                                  uint8_t target_id,
                                  uint8_t* seq_num);

/**
 * \brief           Get current sequence number for a route (without incrementing)
 * \param[in]       ctx: Sequence number context
 * \param[in]       target_id: Target node ID (route identifier)
 * \param[out]      seq_num: Pointer to store sequence number
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_sequence_get_current(const xgl_sequence_ctx_t* ctx,
                                     uint8_t target_id,
                                     uint8_t* seq_num);

/**
 * \brief           Reset sequence number for a route
 * \param[in,out]   ctx: Sequence number context
 * \param[in]       target_id: Target node ID (route identifier)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_sequence_reset(xgl_sequence_ctx_t* ctx,
                               uint8_t target_id);

/**
 * \brief           Reset all sequence numbers
 * \param[in,out]   ctx: Sequence number context
 */
void xgl_sequence_reset_all(xgl_sequence_ctx_t* ctx);

/**
 * \brief           Check if sequence number is valid (within expected range)
 * \param[in]       expected: Expected sequence number
 * \param[in]       received: Received sequence number
 * \param[in]       window_size: Window size for validation
 * \return          true if sequence number is valid, false otherwise
 * \note            Handles wraparound correctly
 */
bool xgl_sequence_is_valid(uint8_t expected,
                           uint8_t received,
                           uint8_t window_size);

/**
 * \brief           Calculate sequence number difference (handles wraparound)
 * \param[in]       seq1: First sequence number
 * \param[in]       seq2: Second sequence number
 * \return          Difference (seq1 - seq2) with wraparound handling
 * \note            Returns signed difference in range [-128, 127]
 */
int16_t xgl_sequence_diff(uint8_t seq1, uint8_t seq2);

/**
 * \brief           Increment sequence number (handles wraparound)
 * \param[in]       seq_num: Current sequence number
 * \return          Next sequence number (wraps to 0 after 255)
 */
static inline uint8_t xgl_sequence_increment(uint8_t seq_num) {
    return (seq_num + 1) & 0xFF;  /* Wraparound at 256 */
}

/**
 * \brief           Compare sequence numbers (handles wraparound)
 * \param[in]       seq1: First sequence number
 * \param[in]       seq2: Second sequence number
 * \return          < 0 if seq1 < seq2, 0 if equal, > 0 if seq1 > seq2
 * \note            Uses circular comparison with wraparound
 */
static inline int xgl_sequence_compare(uint8_t seq1, uint8_t seq2) {
    int16_t diff = (int16_t)seq1 - (int16_t)seq2;
    
    /* Handle wraparound: if difference is > 128, it wrapped backwards */
    if (diff > 128) {
        diff -= 256;
    } else if (diff < -128) {
        diff += 256;
    }
    
    return diff;
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_SEQUENCE_H */
