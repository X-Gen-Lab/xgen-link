/**
 * \file            xgl_reliable_ack.c
 * \brief           Reliable queue ACK range handling
 * \author          X-Gen Lab
 */

#include <xgl/internal/xgl_reliable.h>

size_t xgl_reliable_remove_ack_ranges(xgl_reliable_queue_t* queue,
                                      uint16_t target_id,
                                      uint32_t largest_ack,
                                      const xgl_wire_ack_range_t* ranges,
                                      size_t range_count) {
    if (queue == NULL || (ranges == NULL && range_count > 0U)) {
        return 0U;
    }

    size_t removed = 0U;
    uint64_t next_high = largest_ack;

    for (size_t i = 0; i < range_count; ++i) {
        if (ranges[i].length == 0U) {
            continue;
        }

        uint64_t high = next_high;
        if (i > 0U) {
            uint64_t skip = (uint64_t)ranges[i].gap + 1U;
            if (high < skip) {
                break;
            }
            high -= skip;
        }

        uint64_t low = 0U;
        if (high + 1U > ranges[i].length) {
            low = high - (uint64_t)ranges[i].length + 1U;
        }

        for (uint64_t packet_number = high;; --packet_number) {
            if (xgl_reliable_remove_packet_number(queue,
                                                  (uint32_t)packet_number,
                                                  target_id) == XGL_OK) {
                removed++;
            }

            if (packet_number == low) {
                break;
            }
        }

        next_high = low;
    }

    return removed;
}
