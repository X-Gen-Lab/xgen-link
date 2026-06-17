/**
 * \file            xgl_reliable_timeout.c
 * \brief           Reliable queue timeout and backoff handling
 */

#include <xgl/internal/xgl_reliable.h>

#include "xgl_reliable_internal.h"

/**
 * \brief           Process timeouts and retransmit packets
 */
uint32_t xgl_reliable_process_timeouts(xgl_reliable_queue_t *queue,
                                       uint32_t current_time_ms,
                                       xgl_reliable_packet_t **retry_exhausted)
{
    if (queue == NULL) {
        return 0;
    }

    uint32_t retransmit_count = 0;

    /* Clear retry exhausted output */
    if (retry_exhausted != NULL) {
        *retry_exhausted = NULL;
    }

    /* Iterate through wait-ACK list */
    xgl_list_node_t *node;
    xgl_list_node_t *tmp;
    XGL_LIST_FOR_EACH_SAFE(&queue->wait_ack_list, node, tmp)
    {
        xgl_reliable_packet_t *packet =
            XGL_LIST_ENTRY(node, xgl_reliable_packet_t, node);

        /* Skip if packet hasn't been sent yet */
        if (packet->send_timestamp == 0) {
            continue;
        }

        /* Calculate elapsed time */
        uint32_t elapsed_ms = current_time_ms - packet->send_timestamp;

        /* Check if timeout occurred */
        if (elapsed_ms >= (uint32_t) packet->timeout_ms) {
            /* Check if retry count exhausted */
            if (packet->retry_count >= queue->max_retry_count) {
                /* Remove from list */
                xgl_list_remove(&queue->wait_ack_list, node);
                reliable_unindex_packet(queue, packet);

                /* Return packet to caller for error handling */
                if (retry_exhausted != NULL && *retry_exhausted == NULL) {
                    *retry_exhausted = packet;
                } else {
                    /* Free packet if caller doesn't want it */
                    reliable_free_packet(queue, packet);
                }

                continue;
            }

            /* Increment retry count */
            packet->retry_count++;

            /* Apply exponential backoff */
            packet->timeout_ms = xgl_reliable_calc_backoff(
                packet->initial_timeout_ms, packet->retry_count);

            /* Update send timestamp */
            packet->send_timestamp = current_time_ms;

            /* Retransmit packet */
            if (packet->phy != NULL && packet->phy->tx != NULL) {
                packet->phy->tx(packet->data, packet->data_len,
                                packet->phy->user_data);
                retransmit_count++;
            }
        }
    }

    return retransmit_count;
}

/**
 * \brief           Calculate exponential backoff timeout
 */
int32_t xgl_reliable_calc_backoff(int32_t initial_timeout_ms,
                                  uint8_t retry_count)
{
    /* Exponential backoff: timeout = initial_timeout * 2^retry_count */
    int32_t backoff = initial_timeout_ms;

    /* Limit retry_count to prevent overflow */
    if (retry_count > 10) {
        retry_count = 10;
    }

    /* Calculate 2^retry_count */
    for (uint8_t i = 0; i < retry_count; i++) {
        backoff *= 2;

        /* Prevent overflow and cap at reasonable maximum */
        if (backoff > 30000) { /* 30 seconds max */
            backoff = 30000;
            break;
        }
    }

    return backoff;
}
