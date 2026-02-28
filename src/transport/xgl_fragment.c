/**
 * \file            xgl_fragment.c
 * \brief           Packet Fragmentation and Reassembly Implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_fragment.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/* Helper Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Allocate memory using allocator or malloc
 */
static void* fragment_malloc(xgl_allocator_t* allocator, size_t size) {
    if (allocator != NULL && allocator->malloc != NULL) {
        return allocator->malloc(size);
    }
    return malloc(size);
}

/**
 * \brief           Free memory using allocator or free
 */
static void fragment_free(xgl_allocator_t* allocator, void* ptr) {
    if (ptr == NULL) {
        return;
    }
    
    if (allocator != NULL && allocator->free != NULL) {
        allocator->free(ptr);
    } else {
        free(ptr);
    }
}

/**
 * \brief           Free reassembly buffer and its data
 */
static void free_reassembly_buffer(xgl_fragment_manager_t* manager,
                                  xgl_reassembly_buffer_t* buffer) {
    if (buffer == NULL) {
        return;
    }
    
    /* Free data buffer */
    if (buffer->data != NULL) {
        fragment_free(manager->allocator, buffer->data);
        buffer->data = NULL;
    }
    
    /* Free received bitmap */
    if (buffer->received_bitmap != NULL) {
        fragment_free(manager->allocator, buffer->received_bitmap);
        buffer->received_bitmap = NULL;
    }
    
    /* Free buffer structure */
    fragment_free(manager->allocator, buffer);
}

/**
 * \brief           Find reassembly buffer by fragment ID and source ID
 */
static xgl_reassembly_buffer_t* find_reassembly_buffer(
    xgl_fragment_manager_t* manager,
    uint8_t fragment_id,
    uint8_t source_id) {
    
    if (manager == NULL) {
        return NULL;
    }
    
    /* Search for matching reassembly buffer */
    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&manager->reassembly_list, node) {
        xgl_reassembly_buffer_t* buffer = 
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
        
        if (buffer->fragment_id == fragment_id && 
            buffer->source_id == source_id) {
            return buffer;
        }
    }
    
    return NULL;
}

/**
 * \brief           Check if fragment is already received
 */
static bool is_fragment_received(xgl_reassembly_buffer_t* buffer,
                                uint8_t fragment_index) {
    if (buffer == NULL || buffer->received_bitmap == NULL) {
        return false;
    }
    
    /* Calculate byte and bit position */
    size_t byte_index = fragment_index / 8;
    uint8_t bit_mask = 1 << (fragment_index % 8);
    
    return (buffer->received_bitmap[byte_index] & bit_mask) != 0;
}

/**
 * \brief           Mark fragment as received
 */
static void mark_fragment_received(xgl_reassembly_buffer_t* buffer,
                                  uint8_t fragment_index) {
    if (buffer == NULL || buffer->received_bitmap == NULL) {
        return;
    }
    
    /* Calculate byte and bit position */
    size_t byte_index = fragment_index / 8;
    uint8_t bit_mask = 1 << (fragment_index % 8);
    
    buffer->received_bitmap[byte_index] |= bit_mask;
}

/*---------------------------------------------------------------------------*/
/* Fragmentation Manager Functions                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize fragmentation manager
 */
xgl_error_t xgl_fragment_init(xgl_fragment_manager_t* manager,
                              size_t max_reassembly_buffers,
                              uint32_t reassembly_timeout_ms,
                              xgl_allocator_t* allocator) {
    if (manager == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Initialize reassembly list */
    xgl_list_init(&manager->reassembly_list);
    
    /* Initialize fragment ID counter */
    manager->next_fragment_id = 0;
    
    /* Store configuration */
    manager->max_reassembly_buffers = max_reassembly_buffers;
    manager->reassembly_timeout_ms = reassembly_timeout_ms;
    manager->allocator = allocator;
    
    return XGL_OK;
}

/**
 * \brief           Destroy fragmentation manager
 */
void xgl_fragment_destroy(xgl_fragment_manager_t* manager) {
    if (manager == NULL) {
        return;
    }
    
    /* Clear all reassembly buffers */
    xgl_fragment_clear_reassembly(manager);
}

/**
 * \brief           Fragment data into multiple packets
 */
xgl_error_t xgl_fragment_data(xgl_fragment_manager_t* manager,
                              const uint8_t* data,
                              size_t data_len,
                              size_t max_fragment_size,
                              uint8_t** fragments,
                              size_t* fragment_lens,
                              size_t* fragment_count,
                              uint8_t* fragment_id) {
    if (manager == NULL || data == NULL || data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    if (fragments == NULL || fragment_lens == NULL || 
        fragment_count == NULL || fragment_id == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Calculate fragment header size */
    size_t header_size = sizeof(xgl_fragment_info_t);
    
    /* Check if fragmentation is needed */
    if (data_len <= max_fragment_size) {
        return XGL_ERR_INVALID_PARAM;  /* Data doesn't need fragmentation */
    }
    
    /* Calculate payload size per fragment */
    size_t payload_per_fragment = max_fragment_size - header_size;
    if (payload_per_fragment == 0) {
        return XGL_ERR_BUFFER_TOO_SMALL;
    }
    
    /* Calculate number of fragments needed */
    size_t num_fragments = (data_len + payload_per_fragment - 1) / payload_per_fragment;
    
    if (num_fragments > 255) {
        return XGL_ERR_BUFFER_TOO_SMALL;  /* Too many fragments */
    }
    
    if (num_fragments > *fragment_count) {
        return XGL_ERR_BUFFER_TOO_SMALL;  /* Output array too small */
    }
    
    /* Assign fragment ID */
    *fragment_id = manager->next_fragment_id++;
    
    /* Create fragments */
    size_t offset = 0;
    for (size_t i = 0; i < num_fragments; i++) {
        /* Calculate fragment payload size */
        size_t remaining = data_len - offset;
        size_t payload_size = (remaining < payload_per_fragment) ? 
                             remaining : payload_per_fragment;
        
        /* Allocate fragment buffer */
        size_t fragment_size = header_size + payload_size;
        uint8_t* fragment = (uint8_t*)fragment_malloc(manager->allocator, 
                                                      fragment_size);
        if (fragment == NULL) {
            /* Free previously allocated fragments */
            for (size_t j = 0; j < i; j++) {
                fragment_free(manager->allocator, fragments[j]);
                fragments[j] = NULL;
            }
            return XGL_ERR_NO_MEMORY;
        }
        
        /* Fill fragment header */
        xgl_fragment_info_t* frag_info = (xgl_fragment_info_t*)fragment;
        frag_info->fragment_id = *fragment_id;
        frag_info->fragment_index = (uint8_t)i;
        frag_info->total_fragments = (uint8_t)num_fragments;
        frag_info->fragment_offset = (uint16_t)offset;
        
        /* Copy payload data */
        memcpy(fragment + header_size, data + offset, payload_size);
        
        /* Store fragment */
        fragments[i] = fragment;
        fragment_lens[i] = fragment_size;
        
        offset += payload_size;
    }
    
    *fragment_count = num_fragments;
    
    return XGL_OK;
}

/**
 * \brief           Process received fragment
 */
xgl_error_t xgl_fragment_process(xgl_fragment_manager_t* manager,
                                 uint8_t source_id,
                                 uint8_t data_type,
                                 const uint8_t* fragment_data,
                                 size_t fragment_len,
                                 uint8_t** complete_data,
                                 size_t* complete_len) {
    if (manager == NULL || fragment_data == NULL || fragment_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    if (complete_data == NULL || complete_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Parse fragment header */
    if (fragment_len < sizeof(xgl_fragment_info_t)) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    const xgl_fragment_info_t* frag_info = 
        (const xgl_fragment_info_t*)fragment_data;
    
    /* Validate fragment info */
    if (frag_info->fragment_index >= frag_info->total_fragments) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    if (frag_info->total_fragments == 0) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    /* Calculate payload size */
    size_t payload_size = fragment_len - sizeof(xgl_fragment_info_t);
    const uint8_t* payload = fragment_data + sizeof(xgl_fragment_info_t);
    
    /* Find or create reassembly buffer */
    xgl_reassembly_buffer_t* buffer = find_reassembly_buffer(
        manager, frag_info->fragment_id, source_id);
    
    if (buffer == NULL) {
        /* Check if we can create a new reassembly buffer */
        if (xgl_list_count(&manager->reassembly_list) >= 
            manager->max_reassembly_buffers) {
            return XGL_ERR_NO_MEMORY;
        }
        
        /* Create new reassembly buffer */
        buffer = (xgl_reassembly_buffer_t*)fragment_malloc(
            manager->allocator, sizeof(xgl_reassembly_buffer_t));
        
        if (buffer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
        
        memset(buffer, 0, sizeof(xgl_reassembly_buffer_t));
        
        /* Initialize buffer */
        buffer->fragment_id = frag_info->fragment_id;
        buffer->source_id = source_id;
        buffer->data_type = data_type;
        buffer->total_fragments = frag_info->total_fragments;
        buffer->received_count = 0;
        buffer->timeout_ms = manager->reassembly_timeout_ms;
        buffer->first_fragment_time = 0;  /* Will be set below */
        
        /* Allocate received bitmap */
        size_t bitmap_size = (frag_info->total_fragments + 7) / 8;
        buffer->received_bitmap = (uint8_t*)fragment_malloc(
            manager->allocator, bitmap_size);
        
        if (buffer->received_bitmap == NULL) {
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_NO_MEMORY;
        }
        
        memset(buffer->received_bitmap, 0, bitmap_size);
        
        /* Estimate total data size (will be adjusted as fragments arrive) */
        buffer->buffer_size = payload_size * frag_info->total_fragments;
        buffer->data = (uint8_t*)fragment_malloc(manager->allocator, 
                                                 buffer->buffer_size);
        
        if (buffer->data == NULL) {
            fragment_free(manager->allocator, buffer->received_bitmap);
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_NO_MEMORY;
        }
        
        buffer->data_len = 0;
        
        /* Initialize list node and add to list */
        xgl_list_node_init(&buffer->node);
        xgl_list_insert_tail(&manager->reassembly_list, &buffer->node);
    }
    
    /* Check if fragment already received */
    if (is_fragment_received(buffer, frag_info->fragment_index)) {
        return XGL_ERR_BUSY;  /* Duplicate fragment, still waiting */
    }
    
    /* Calculate destination offset */
    size_t dest_offset = frag_info->fragment_offset;
    
    /* Check if buffer is large enough */
    if (dest_offset + payload_size > buffer->buffer_size) {
        /* Reallocate buffer */
        size_t new_size = dest_offset + payload_size;
        uint8_t* new_data = (uint8_t*)fragment_malloc(manager->allocator, 
                                                      new_size);
        
        if (new_data == NULL) {
            return XGL_ERR_NO_MEMORY;
        }
        
        /* Copy existing data */
        if (buffer->data != NULL) {
            memcpy(new_data, buffer->data, buffer->data_len);
            fragment_free(manager->allocator, buffer->data);
        }
        
        buffer->data = new_data;
        buffer->buffer_size = new_size;
    }
    
    /* Copy fragment payload to reassembly buffer */
    memcpy(buffer->data + dest_offset, payload, payload_size);
    
    /* Update data length */
    if (dest_offset + payload_size > buffer->data_len) {
        buffer->data_len = dest_offset + payload_size;
    }
    
    /* Mark fragment as received */
    mark_fragment_received(buffer, frag_info->fragment_index);
    buffer->received_count++;
    
    /* Set first fragment timestamp if this is the first fragment */
    if (buffer->received_count == 1) {
        /* Note: Timestamp should be provided by caller or use platform time */
        buffer->first_fragment_time = 0;  /* Placeholder */
    }
    
    /* Check if reassembly is complete */
    if (buffer->received_count == buffer->total_fragments) {
        /* Reassembly complete */
        *complete_data = buffer->data;
        *complete_len = buffer->data_len;
        
        /* Remove buffer from list */
        xgl_list_remove(&manager->reassembly_list, &buffer->node);
        
        /* Free bitmap and buffer structure (but not data) */
        if (buffer->received_bitmap != NULL) {
            fragment_free(manager->allocator, buffer->received_bitmap);
        }
        fragment_free(manager->allocator, buffer);
        
        return XGL_OK;
    }
    
    /* Still waiting for more fragments */
    return XGL_ERR_BUSY;
}

/**
 * \brief           Process reassembly timeouts
 */
uint32_t xgl_fragment_process_timeouts(xgl_fragment_manager_t* manager,
                                       uint32_t current_time_ms) {
    if (manager == NULL) {
        return 0;
    }
    
    uint32_t timeout_count = 0;
    
    /* Iterate through reassembly buffers */
    xgl_list_node_t* node;
    xgl_list_node_t* tmp;
    XGL_LIST_FOR_EACH_SAFE(&manager->reassembly_list, node, tmp) {
        xgl_reassembly_buffer_t* buffer = 
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
        
        /* Skip if first fragment hasn't been received yet */
        if (buffer->first_fragment_time == 0) {
            continue;
        }
        
        /* Calculate elapsed time */
        uint32_t elapsed_ms = current_time_ms - buffer->first_fragment_time;
        
        /* Check if timeout occurred */
        if (elapsed_ms >= buffer->timeout_ms) {
            /* Remove from list */
            xgl_list_remove(&manager->reassembly_list, node);
            
            /* Free buffer */
            free_reassembly_buffer(manager, buffer);
            
            timeout_count++;
        }
    }
    
    return timeout_count;
}

/**
 * \brief           Get number of active reassembly buffers
 */
size_t xgl_fragment_get_reassembly_count(const xgl_fragment_manager_t* manager) {
    if (manager == NULL) {
        return 0;
    }
    
    return xgl_list_count(&manager->reassembly_list);
}

/**
 * \brief           Clear all reassembly buffers
 */
void xgl_fragment_clear_reassembly(xgl_fragment_manager_t* manager) {
    if (manager == NULL) {
        return;
    }
    
    /* Remove and free all reassembly buffers */
    xgl_list_node_t* node;
    while ((node = xgl_list_remove_head(&manager->reassembly_list)) != NULL) {
        xgl_reassembly_buffer_t* buffer = 
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
        free_reassembly_buffer(manager, buffer);
    }
}

/**
 * \brief           Free fragment data allocated by xgl_fragment_data
 */
void xgl_fragment_free_fragments(xgl_fragment_manager_t* manager,
                                 uint8_t** fragments,
                                 size_t fragment_count) {
    if (manager == NULL || fragments == NULL) {
        return;
    }
    
    for (size_t i = 0; i < fragment_count; i++) {
        if (fragments[i] != NULL) {
            fragment_free(manager->allocator, fragments[i]);
            fragments[i] = NULL;
        }
    }
}

/**
 * \brief           Free complete data allocated by xgl_fragment_process
 */
void xgl_fragment_free_data(xgl_fragment_manager_t* manager,
                            uint8_t* data) {
    if (manager == NULL || data == NULL) {
        return;
    }
    
    fragment_free(manager->allocator, data);
}
