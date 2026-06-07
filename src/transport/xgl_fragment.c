/**
 * \file            xgl_fragment.c
 * \brief           Packet Fragmentation and Reassembly Implementation
 * \author          Nexus Team
 */

#include <xgl/xgl_fragment.h>
#include <xgl/xgl_time.h>
#include <xgl/xgl_serialize.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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

    if (manager != NULL && buffer->reserved_size <= manager->current_reassembly_bytes) {
        manager->current_reassembly_bytes -= buffer->reserved_size;
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
    uint16_t source_id) {
    
    if (manager == NULL) {
        return NULL;
    }
    
    /* Search for matching reassembly buffer */
    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&manager->reassembly_list, node) {
        xgl_reassembly_buffer_t* buffer = 
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);
        
        if (!buffer->uses_fragment_ext &&
            buffer->fragment_id == fragment_id &&
            buffer->source_id == source_id) {
            return buffer;
        }
    }
    
    return NULL;
}

static xgl_reassembly_buffer_t* find_reassembly_buffer_ext(
    xgl_fragment_manager_t* manager,
    uint16_t source_id,
    uint32_t connection_id,
    uint32_t session_epoch,
    uint32_t message_id) {

    if (manager == NULL) {
        return NULL;
    }

    xgl_list_node_t* node;
    XGL_LIST_FOR_EACH(&manager->reassembly_list, node) {
        xgl_reassembly_buffer_t* buffer =
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);

        if (buffer->uses_fragment_ext &&
            buffer->source_id == source_id &&
            buffer->connection_id == connection_id &&
            buffer->session_epoch == session_epoch &&
            buffer->message_id == message_id) {
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
    uint8_t bit_mask = (uint8_t)(1U << (fragment_index % 8U));
    
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
    uint8_t bit_mask = (uint8_t)(1U << (fragment_index % 8U));
    
    buffer->received_bitmap[byte_index] |= bit_mask;
}

static bool is_byte_received(const xgl_reassembly_buffer_t* buffer,
                             size_t byte_offset) {
    if (buffer == NULL || buffer->received_bitmap == NULL) {
        return false;
    }

    size_t byte_index = byte_offset / 8U;
    uint8_t bit_mask = (uint8_t)(1U << (byte_offset % 8U));
    return (buffer->received_bitmap[byte_index] & bit_mask) != 0U;
}

static void mark_byte_received(xgl_reassembly_buffer_t* buffer,
                               size_t byte_offset) {
    if (buffer == NULL || buffer->received_bitmap == NULL) {
        return;
    }

    size_t byte_index = byte_offset / 8U;
    uint8_t bit_mask = (uint8_t)(1U << (byte_offset % 8U));
    buffer->received_bitmap[byte_index] |= bit_mask;
}

static void encode_fragment_info(uint8_t* buffer,
                                 const xgl_fragment_info_t* info) {
    buffer[0] = info->fragment_id;
    buffer[1] = info->fragment_index;
    buffer[2] = info->total_fragments;
    xgl_serialize_u16_le(&buffer[3], info->fragment_offset);
}

static void decode_fragment_info(xgl_fragment_info_t* info,
                                 const uint8_t* buffer) {
    info->fragment_id = buffer[0];
    info->fragment_index = buffer[1];
    info->total_fragments = buffer[2];
    info->fragment_offset = xgl_deserialize_u16_le(&buffer[3]);
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
    manager->max_message_size = 0;
    manager->max_reassembly_bytes = 0;
    manager->current_reassembly_bytes = 0;
    
    return XGL_OK;
}

xgl_error_t xgl_fragment_set_limits(xgl_fragment_manager_t* manager,
                                    size_t max_message_size,
                                    size_t max_reassembly_bytes) {
    if (manager == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    if (max_reassembly_bytes != 0U && max_message_size > max_reassembly_bytes) {
        return XGL_ERR_INVALID_PARAM;
    }

    manager->max_message_size = max_message_size;
    manager->max_reassembly_bytes = max_reassembly_bytes;

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
    /* Check for NULL pointers first */
    if (manager == NULL || data == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (fragments == NULL || fragment_lens == NULL || 
        fragment_count == NULL || fragment_id == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Check for invalid parameters */
    if (data_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Calculate fragment header size */
    size_t header_size = XGL_FRAGMENT_HEADER_SIZE;
    
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
        if (offset > UINT16_MAX) {
            for (size_t j = 0; j < i; j++) {
                fragment_free(manager->allocator, fragments[j]);
                fragments[j] = NULL;
            }
            return XGL_ERR_BUFFER_TOO_SMALL;
        }

        xgl_fragment_info_t frag_info = {
            .fragment_id = *fragment_id,
            .fragment_index = (uint8_t)i,
            .total_fragments = (uint8_t)num_fragments,
            .fragment_offset = (uint16_t)offset
        };
        encode_fragment_info(fragment, &frag_info);
        
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
                                 uint16_t source_id,
                                 uint8_t data_type,
                                 const uint8_t* fragment_data,
                                 size_t fragment_len,
                                 uint8_t** complete_data,
                                 size_t* complete_len,
                                 uint32_t current_time_ms) {
    if (manager == NULL || fragment_data == NULL || fragment_len == 0) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    if (complete_data == NULL || complete_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Parse fragment header */
    if (fragment_len < XGL_FRAGMENT_HEADER_SIZE) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    xgl_fragment_info_t frag_info_storage;
    decode_fragment_info(&frag_info_storage, fragment_data);
    const xgl_fragment_info_t* frag_info = &frag_info_storage;
    
    /* Validate fragment info */
    if (frag_info->fragment_index >= frag_info->total_fragments) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    if (frag_info->total_fragments == 0) {
        return XGL_ERR_INVALID_FRAME;
    }
    
    /* Calculate payload size */
    size_t payload_size = fragment_len - XGL_FRAGMENT_HEADER_SIZE;
    const uint8_t* payload = fragment_data + XGL_FRAGMENT_HEADER_SIZE;

    if (frag_info->fragment_index == 0U && frag_info->fragment_offset != 0U) {
        return XGL_ERR_INVALID_FRAME;
    }
    
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

        if (frag_info->fragment_index == 0U) {
            buffer->expected_payload_size = payload_size;
        } else {
            if (frag_info->fragment_offset == 0U ||
                (frag_info->fragment_offset % frag_info->fragment_index) != 0U) {
                fragment_free(manager->allocator, buffer);
                return XGL_ERR_INVALID_FRAME;
            }
            buffer->expected_payload_size =
                frag_info->fragment_offset / frag_info->fragment_index;
            if (buffer->expected_payload_size == 0U) {
                fragment_free(manager->allocator, buffer);
                return XGL_ERR_INVALID_FRAME;
            }
        }
        
        size_t reserved_size = buffer->expected_payload_size *
                               (size_t)frag_info->total_fragments;
        if (manager->max_message_size != 0U &&
            reserved_size > manager->max_message_size) {
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_BUFFER_TOO_SMALL;
        }
        if (manager->max_reassembly_bytes != 0U &&
            reserved_size > manager->max_reassembly_bytes - manager->current_reassembly_bytes) {
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_NO_MEMORY;
        }

        /* Allocate received bitmap */
        size_t bitmap_size = ((size_t)frag_info->total_fragments + 7U) / 8U;
        buffer->received_bitmap = (uint8_t*)fragment_malloc(
            manager->allocator, bitmap_size);
        
        if (buffer->received_bitmap == NULL) {
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_NO_MEMORY;
        }
        
        memset(buffer->received_bitmap, 0, bitmap_size);
        
        /* Reserve the estimated full message size up front. */
        buffer->reserved_size = reserved_size;
        buffer->buffer_size = reserved_size;
        buffer->data = (uint8_t*)fragment_malloc(manager->allocator, 
                                                 buffer->buffer_size);
        
        if (buffer->data == NULL) {
            fragment_free(manager->allocator, buffer->received_bitmap);
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_NO_MEMORY;
        }

        manager->current_reassembly_bytes += buffer->reserved_size;
        
        buffer->data_len = 0;
        
        /* Initialize list node and add to list */
        xgl_list_node_init(&buffer->node);
        xgl_list_insert_tail(&manager->reassembly_list, &buffer->node);
    }

    if (buffer->data_type != data_type ||
        buffer->total_fragments != frag_info->total_fragments) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (buffer->expected_payload_size == 0U) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t expected_offset = (size_t)frag_info->fragment_index *
                             buffer->expected_payload_size;
    if (frag_info->fragment_offset != expected_offset) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (frag_info->fragment_index + 1U < frag_info->total_fragments &&
        payload_size != buffer->expected_payload_size) {
        return XGL_ERR_INVALID_FRAME;
    }

    if (payload_size > buffer->expected_payload_size) {
        return XGL_ERR_INVALID_FRAME;
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
    
    /* Set initial send timestamp */
    if (buffer->received_count == 1) {
        /* Capture timestamp when first fragment is received */
        buffer->first_fragment_time = (current_time_ms == 0) ? xgl_time_ms() : current_time_ms;
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
        if (buffer->reserved_size <= manager->current_reassembly_bytes) {
            manager->current_reassembly_bytes -= buffer->reserved_size;
        }
        fragment_free(manager->allocator, buffer);
        
        return XGL_OK;
    }
    
    /* Still waiting for more fragments */
    return XGL_ERR_BUSY;
}

xgl_error_t xgl_fragment_process_ext(xgl_fragment_manager_t* manager,
                                     uint16_t source_id,
                                     uint32_t connection_id,
                                     uint32_t session_epoch,
                                     uint8_t data_type,
                                     uint32_t message_id,
                                     uint32_t fragment_offset,
                                     uint32_t message_len,
                                     const uint8_t* fragment_payload,
                                     size_t fragment_payload_len,
                                     uint8_t** complete_data,
                                     size_t* complete_len,
                                     uint32_t current_time_ms) {
    if (manager == NULL || fragment_payload == NULL || fragment_payload_len == 0U) {
        return XGL_ERR_INVALID_PARAM;
    }

    if (complete_data == NULL || complete_len == NULL) {
        return XGL_ERR_NULL_POINTER;
    }

    *complete_data = NULL;
    *complete_len = 0U;

    if (message_len == 0U ||
        fragment_offset > message_len ||
        fragment_payload_len > (size_t)message_len - (size_t)fragment_offset) {
        return XGL_ERR_INVALID_FRAME;
    }

    xgl_reassembly_buffer_t* buffer = find_reassembly_buffer_ext(manager,
                                                                 source_id,
                                                                 connection_id,
                                                                 session_epoch,
                                                                 message_id);
    if (buffer == NULL) {
        if (xgl_list_count(&manager->reassembly_list) >=
            manager->max_reassembly_buffers) {
            return XGL_ERR_NO_MEMORY;
        }

        if (manager->max_message_size != 0U &&
            (size_t)message_len > manager->max_message_size) {
            return XGL_ERR_BUFFER_TOO_SMALL;
        }

        if (manager->max_reassembly_bytes != 0U &&
            (manager->current_reassembly_bytes > manager->max_reassembly_bytes ||
             (size_t)message_len > manager->max_reassembly_bytes - manager->current_reassembly_bytes)) {
            return XGL_ERR_NO_MEMORY;
        }

        buffer = (xgl_reassembly_buffer_t*)fragment_malloc(manager->allocator,
                                                           sizeof(xgl_reassembly_buffer_t));
        if (buffer == NULL) {
            return XGL_ERR_NO_MEMORY;
        }

        memset(buffer, 0, sizeof(xgl_reassembly_buffer_t));
        buffer->uses_fragment_ext = true;
        buffer->source_id = source_id;
        buffer->connection_id = connection_id;
        buffer->session_epoch = session_epoch;
        buffer->message_id = message_id;
        buffer->data_type = data_type;
        buffer->timeout_ms = manager->reassembly_timeout_ms;
        buffer->buffer_size = message_len;
        buffer->reserved_size = message_len;
        buffer->data_len = message_len;

        size_t bitmap_size = ((size_t)message_len + 7U) / 8U;
        buffer->received_bitmap = (uint8_t*)fragment_malloc(manager->allocator,
                                                            bitmap_size);
        if (buffer->received_bitmap == NULL) {
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_NO_MEMORY;
        }
        memset(buffer->received_bitmap, 0, bitmap_size);

        buffer->data = (uint8_t*)fragment_malloc(manager->allocator,
                                                 buffer->buffer_size);
        if (buffer->data == NULL) {
            fragment_free(manager->allocator, buffer->received_bitmap);
            fragment_free(manager->allocator, buffer);
            return XGL_ERR_NO_MEMORY;
        }

        manager->current_reassembly_bytes += buffer->reserved_size;

        xgl_list_node_init(&buffer->node);
        xgl_list_insert_tail(&manager->reassembly_list, &buffer->node);
    }

    if (buffer->data_type != data_type ||
        buffer->buffer_size != (size_t)message_len) {
        return XGL_ERR_INVALID_FRAME;
    }

    size_t start = fragment_offset;
    size_t end = start + fragment_payload_len;
    for (size_t i = start; i < end; ++i) {
        if (is_byte_received(buffer, i)) {
            return XGL_ERR_BUSY;
        }
    }

    bool is_first_received_range = (buffer->received_bytes == 0U);

    memcpy(&buffer->data[start], fragment_payload, fragment_payload_len);
    for (size_t i = start; i < end; ++i) {
        mark_byte_received(buffer, i);
    }
    buffer->received_bytes += fragment_payload_len;

    if (is_first_received_range) {
        buffer->first_fragment_time = (current_time_ms == 0U) ?
                                      xgl_time_ms() : current_time_ms;
    }

    if (buffer->received_bytes == buffer->buffer_size) {
        *complete_data = buffer->data;
        *complete_len = buffer->data_len;

        xgl_list_remove(&manager->reassembly_list, &buffer->node);

        if (buffer->received_bitmap != NULL) {
            fragment_free(manager->allocator, buffer->received_bitmap);
        }
        if (buffer->reserved_size <= manager->current_reassembly_bytes) {
            manager->current_reassembly_bytes -= buffer->reserved_size;
        }
        fragment_free(manager->allocator, buffer);

        return XGL_OK;
    }

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

size_t xgl_fragment_clear_reassembly_scope(xgl_fragment_manager_t* manager,
                                           uint16_t source_id,
                                           uint32_t connection_id,
                                           uint32_t session_epoch) {
    if (manager == NULL) {
        return 0U;
    }

    size_t cleared = 0U;
    xgl_list_node_t* node;
    xgl_list_node_t* tmp;
    XGL_LIST_FOR_EACH_SAFE(&manager->reassembly_list, node, tmp) {
        xgl_reassembly_buffer_t* buffer =
            XGL_LIST_ENTRY(node, xgl_reassembly_buffer_t, node);

        if (buffer->uses_fragment_ext &&
            buffer->source_id == source_id &&
            buffer->connection_id == connection_id &&
            buffer->session_epoch == session_epoch) {
            xgl_list_remove(&manager->reassembly_list, node);
            free_reassembly_buffer(manager, buffer);
            cleared++;
        }
    }

    return cleared;
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
