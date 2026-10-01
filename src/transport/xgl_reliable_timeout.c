/**
 * \file            xgl_reliable_timeout.c
 * \brief           Reliable queue timeout and backoff handling
 */

#include <xgl/internal/xgl_reliable.h>

#include "xgl_reliable_internal.h"

/* Parameter order follows the documented protocol fields and units. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
/**
 * \brief           Calculate exponential backoff without signed overflow
 * \param[in]       initial_timeout_ms: Initial timeout; nonpositive means zero
 * \param[in]       retry_count: Retry exponent, limited to ten
 * \return          Timeout saturated to the inclusive range 0 to 30000 ms
 */
int32_t xgl_reliable_calc_backoff(int32_t initial_timeout_ms,
                                  uint8_t retry_count) {
    /* NOLINTEND(bugprone-easily-swappable-parameters) */
    const int32_t maximum_timeout_ms = 30000;
    if (initial_timeout_ms <= 0) {
        return 0;
    }
    if (initial_timeout_ms >= maximum_timeout_ms) {
        return maximum_timeout_ms;
    }

    int32_t backoff = initial_timeout_ms;
    /* Preserve the bounded exponent used by the reliable queue helpers. */
    if (retry_count > 10) {
        retry_count = 10;
    }

    for (uint8_t i = 0; i < retry_count; i++) {
        /* Check before multiplication, including large valid initial values. */
        if (backoff >= maximum_timeout_ms / 2) {
            return maximum_timeout_ms;
        }
        backoff *= 2;
    }

    return backoff;
}
