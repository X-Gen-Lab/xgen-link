/**
 * \file            xgl_fragment_range.c
 * \brief           Fragment received range tracking
 * \author          X-Gen Lab
 */

#include "xgl_fragment_internal.h"

static bool fragment_range_overlaps(const xgl_fragment_received_range_t* range,
                                    size_t start, size_t end) {
    return range != NULL && start < range->end && end > range->start;
}

static void fragment_remove_range_at(xgl_reassembly_buffer_t* buffer,
                                     size_t index) {
    if (buffer == NULL || index >= buffer->received_range_count) {
        return;
    }

    for (size_t i = index + 1U; i < buffer->received_range_count; ++i) {
        buffer->received_ranges[i - 1U] = buffer->received_ranges[i];
    }
    buffer->received_range_count--;
}

/**
 * \brief           Insert a disjoint range or identify a fully covered
 *                  duplicate
 * \param[in,out]   buffer: Reassembly buffer with sorted disjoint ranges
 * \param[in]       start: Inclusive new range start
 * \param[in]       end: Exclusive new range end
 * \return          XGL_OK on insertion, BUSY for full coverage, INVALID_FRAME
 *                  for partial overlap, or NO_MEMORY when ranges are exhausted
 */
xgl_error_t fragment_insert_received_range(xgl_reassembly_buffer_t* buffer,
                                           size_t start, size_t end) {
    if (buffer == NULL || start >= end) {
        return XGL_ERR_INVALID_PARAM;
    }

    for (size_t i = 0; i < buffer->received_range_count; ++i) {
        const xgl_fragment_received_range_t* range =
            &buffer->received_ranges[i];
        if (fragment_range_overlaps(range, start, end)) {
            if (start >= range->start && end <= range->end) {
                return XGL_ERR_BUSY;
            }
            return XGL_ERR_INVALID_FRAME;
        }
    }

    size_t insert_index = 0U;
    while (insert_index < buffer->received_range_count &&
           buffer->received_ranges[insert_index].start < start) {
        insert_index++;
    }

    if (insert_index > 0U &&
        buffer->received_ranges[insert_index - 1U].end == start) {
        buffer->received_ranges[insert_index - 1U].end = end;
        if (insert_index < buffer->received_range_count &&
            buffer->received_ranges[insert_index].start == end) {
            buffer->received_ranges[insert_index - 1U].end =
                buffer->received_ranges[insert_index].end;
            fragment_remove_range_at(buffer, insert_index);
        }
        return XGL_OK;
    }

    if (insert_index < buffer->received_range_count &&
        buffer->received_ranges[insert_index].start == end) {
        buffer->received_ranges[insert_index].start = start;
        return XGL_OK;
    }

    if (buffer->received_range_count >= XGL_FRAGMENT_MAX_RECEIVED_RANGES) {
        return XGL_ERR_NO_MEMORY;
    }

    for (size_t i = buffer->received_range_count; i > insert_index; --i) {
        buffer->received_ranges[i] = buffer->received_ranges[i - 1U];
    }
    buffer->received_ranges[insert_index].start = start;
    buffer->received_ranges[insert_index].end = end;
    buffer->received_range_count++;

    return XGL_OK;
}
