/**
 * \file            xgl_hashtable.c
 * \brief           Hash table implementation for O(1) route lookup
 * \author          Nexus Team
 */

#include <xgl/xgl_hashtable.h>
#include <xgl/xgl_error.h>
#include <string.h>
#include <stdlib.h>

/*---------------------------------------------------------------------------*/
/* Private Helper Functions                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Hash function for uint8_t keys
 * \details         Simple modulo hash for small key space (0-255)
 */
static inline size_t xgl_hash(uint8_t key, size_t size) {
    /* For power-of-2 sizes, use bitwise AND for fast modulo */
    return key & (size - 1);
}

/**
 * \brief           Check if size is power of 2
 */
static inline bool xgl_is_power_of_2(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

/**
 * \brief           Allocate memory using table's allocator
 */
static void* xgl_hashtable_alloc(xgl_hashtable_t* table, size_t size) {
    if (table->allocator && table->allocator->malloc) {
        return table->allocator->malloc(size);
    }
    return malloc(size);
}

/**
 * \brief           Free memory using table's allocator
 */
static void xgl_hashtable_free(xgl_hashtable_t* table, void* ptr) {
    if (table->allocator && table->allocator->free) {
        table->allocator->free(ptr);
    } else {
        free(ptr);
    }
}

/*---------------------------------------------------------------------------*/
/* Public API Implementation                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize hash table
 */
xgl_error_t xgl_hashtable_init(xgl_hashtable_t* table,
                               size_t size,
                               xgl_allocator_t* allocator) {
    if (table == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Validate size is power of 2 */
    if (!xgl_is_power_of_2(size)) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Initialize structure */
    table->size = size;
    table->count = 0;
    table->allocator = allocator;
    
    /* Allocate buckets array */
    table->buckets = (xgl_hashtable_entry_t**)xgl_hashtable_alloc(
        table, 
        sizeof(xgl_hashtable_entry_t*) * size
    );
    
    if (table->buckets == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    
    /* Initialize all buckets to NULL */
    memset(table->buckets, 0, sizeof(xgl_hashtable_entry_t*) * size);
    
    return XGL_OK;
}

/**
 * \brief           Destroy hash table and free resources
 */
void xgl_hashtable_destroy(xgl_hashtable_t* table) {
    if (table == NULL || table->buckets == NULL) {
        return;
    }
    
    /* Free all entries in all buckets */
    for (size_t i = 0; i < table->size; i++) {
        xgl_hashtable_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            xgl_hashtable_entry_t* next = entry->next;
            xgl_hashtable_free(table, entry);
            entry = next;
        }
    }
    
    /* Free buckets array */
    xgl_hashtable_free(table, table->buckets);
    
    /* Reset structure */
    table->buckets = NULL;
    table->size = 0;
    table->count = 0;
}


/**
 * \brief           Insert or update entry in hash table
 */
xgl_error_t xgl_hashtable_insert(xgl_hashtable_t* table,
                                 uint8_t key,
                                 xgl_route_item_t* value) {
    if (table == NULL || table->buckets == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    if (value == NULL) {
        return XGL_ERR_INVALID_PARAM;
    }
    
    /* Calculate hash */
    size_t index = xgl_hash(key, table->size);
    
    /* Check if key already exists (update case) */
    xgl_hashtable_entry_t* entry = table->buckets[index];
    while (entry != NULL) {
        if (entry->key == key) {
            /* Update existing entry */
            entry->value = value;
            return XGL_OK;
        }
        entry = entry->next;
    }
    
    /* Key not found, insert new entry */
    xgl_hashtable_entry_t* new_entry = (xgl_hashtable_entry_t*)xgl_hashtable_alloc(
        table,
        sizeof(xgl_hashtable_entry_t)
    );
    
    if (new_entry == NULL) {
        return XGL_ERR_NO_MEMORY;
    }
    
    /* Initialize new entry */
    new_entry->key = key;
    new_entry->value = value;
    new_entry->next = table->buckets[index];
    
    /* Insert at head of chain */
    table->buckets[index] = new_entry;
    table->count++;
    
    return XGL_OK;
}

/**
 * \brief           Lookup entry in hash table (O(1) average)
 */
xgl_route_item_t* xgl_hashtable_lookup(const xgl_hashtable_t* table,
                                       uint8_t key) {
    if (table == NULL || table->buckets == NULL) {
        return NULL;
    }
    
    /* Calculate hash */
    size_t index = xgl_hash(key, table->size);
    
    /* Search chain for matching key */
    xgl_hashtable_entry_t* entry = table->buckets[index];
    while (entry != NULL) {
        if (entry->key == key) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    /* Key not found */
    return NULL;
}

/**
 * \brief           Remove entry from hash table
 */
xgl_error_t xgl_hashtable_remove(xgl_hashtable_t* table, uint8_t key) {
    if (table == NULL || table->buckets == NULL) {
        return XGL_ERR_NULL_POINTER;
    }
    
    /* Calculate hash */
    size_t index = xgl_hash(key, table->size);
    
    /* Search chain for matching key */
    xgl_hashtable_entry_t* entry = table->buckets[index];
    xgl_hashtable_entry_t* prev = NULL;
    
    while (entry != NULL) {
        if (entry->key == key) {
            /* Found entry, remove it */
            if (prev == NULL) {
                /* Entry is at head of chain */
                table->buckets[index] = entry->next;
            } else {
                /* Entry is in middle or end of chain */
                prev->next = entry->next;
            }
            
            /* Free entry */
            xgl_hashtable_free(table, entry);
            table->count--;
            
            return XGL_OK;
        }
        
        prev = entry;
        entry = entry->next;
    }
    
    /* Key not found */
    return XGL_ERR_ROUTE_NOT_FOUND;
}

/**
 * \brief           Clear all entries from hash table
 */
void xgl_hashtable_clear(xgl_hashtable_t* table) {
    if (table == NULL || table->buckets == NULL) {
        return;
    }
    
    /* Free all entries in all buckets */
    for (size_t i = 0; i < table->size; i++) {
        xgl_hashtable_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            xgl_hashtable_entry_t* next = entry->next;
            xgl_hashtable_free(table, entry);
            entry = next;
        }
        table->buckets[i] = NULL;
    }
    
    /* Reset count */
    table->count = 0;
}
