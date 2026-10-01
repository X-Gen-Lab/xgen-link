/**
 * \file            xgl_protocol_memory.h
 * \brief           Independent storage reservations for protocol resources
 * \author          X-Gen Lab
 */

#ifndef XGL_PROTOCOL_MEMORY_H
#define XGL_PROTOCOL_MEMORY_H

#include <xgl/xgl_build_config.h>

#include <xgen/memory/allocator.h>

/**
 * \brief           Borrowed allocators for independently bounded resources
 * \note            Descriptors outlive the instance. A resource is always
 *                  released through the allocator that supplied its storage.
 */
typedef struct {
    const xgm_allocator_t* peer;
    const xgm_allocator_t* window;
    const xgm_allocator_t* tx_packet;
    const xgm_allocator_t* tx_payload;
    const xgm_allocator_t* scratch;
#if XGL_FEATURE_OUT_OF_ORDER
    const xgm_allocator_t* rx_packet;
    const xgm_allocator_t* rx_payload;
    const xgm_allocator_t* rx_extensions;
#endif
#if XGL_FEATURE_FRAGMENTATION
    const xgm_allocator_t* tx_extensions;
    const xgm_allocator_t* tx_message;
    const xgm_allocator_t* reassembly;
    const xgm_allocator_t* reassembly_payload;
#endif
} xgl_protocol_memory_t;

#endif /* XGL_PROTOCOL_MEMORY_H */
