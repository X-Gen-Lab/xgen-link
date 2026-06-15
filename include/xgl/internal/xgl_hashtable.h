/**
 * \file            xgl_hashtable.h
 * \brief           Hash table for O(1) route lookup
 * \author          Nexus Team
 */

#ifndef XGL_HASHTABLE_H
#define XGL_HASHTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "xgl/xgl_types.h"
#include "xgl/xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Hash Table Configuration                                                  */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default hash table size (must be power of 2)
 */
#define XGL_HASHTABLE_DEFAULT_SIZE  16

/**
 * \brief           Maximum load factor before resize (75%)
 */
#define XGL_HASHTABLE_MAX_LOAD      0.75f

/*---------------------------------------------------------------------------*/
/* Hash Table Entry                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Hash table entry (for chaining collision resolution)
 */
typedef struct xgl_hashtable_entry {
    uint16_t key;                   /**< Target ID (key) */
    xgl_route_item_t* value;        /**< Route item (value) */
    struct xgl_hashtable_entry* next; /**< Next entry in chain */
} xgl_hashtable_entry_t;

/*---------------------------------------------------------------------------*/
/* Hash Table Structure                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Hash table structure
 */
typedef struct {
    xgl_hashtable_entry_t** buckets; /**< Array of bucket pointers */
    size_t size;                    /**< Number of buckets */
    size_t count;                   /**< Number of entries */
    xgl_allocator_t* allocator;     /**< Memory allocator */
} xgl_hashtable_t;

/*---------------------------------------------------------------------------*/
/* Hash Table API                                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize hash table
 * \param[in,out]   table: Hash table structure
 * \param[in]       size: Initial size (must be power of 2)
 * \param[in]       allocator: Memory allocator; NULL fallback is build-policy controlled
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_hashtable_init(xgl_hashtable_t* table,
                               size_t size,
                               xgl_allocator_t* allocator);

/**
 * \brief           Destroy hash table and free resources
 * \param[in]       table: Hash table structure
 */
void xgl_hashtable_destroy(xgl_hashtable_t* table);

/**
 * \brief           Insert or update entry in hash table
 * \param[in,out]   table: Hash table structure
 * \param[in]       key: Target ID (key)
 * \param[in]       value: Route item (value)
 * \return          XGL_OK on success, error code otherwise
 */
xgl_error_t xgl_hashtable_insert(xgl_hashtable_t* table,
                                 uint16_t key,
                                 xgl_route_item_t* value);

/**
 * \brief           Lookup entry in hash table (O(1) average)
 * \param[in]       table: Hash table structure
 * \param[in]       key: Target ID (key)
 * \return          Route item pointer, NULL if not found
 */
xgl_route_item_t* xgl_hashtable_lookup(const xgl_hashtable_t* table,
                                       uint16_t key);

/**
 * \brief           Remove entry from hash table
 * \param[in,out]   table: Hash table structure
 * \param[in]       key: Target ID (key)
 * \return          XGL_OK on success, XGL_ERR_ROUTE_NOT_FOUND if not found
 */
xgl_error_t xgl_hashtable_remove(xgl_hashtable_t* table, uint16_t key);

/**
 * \brief           Clear all entries from hash table
 * \param[in,out]   table: Hash table structure
 */
void xgl_hashtable_clear(xgl_hashtable_t* table);

/**
 * \brief           Get number of entries in hash table
 * \param[in]       table: Hash table structure
 * \return          Number of entries
 */
static inline size_t xgl_hashtable_count(const xgl_hashtable_t* table) {
    return table ? table->count : 0;
}

/**
 * \brief           Check if hash table is empty
 * \param[in]       table: Hash table structure
 * \return          true if empty, false otherwise
 */
static inline bool xgl_hashtable_is_empty(const xgl_hashtable_t* table) {
    return table ? (table->count == 0) : true;
}

#ifdef __cplusplus
}
#endif

#endif /* XGL_HASHTABLE_H */
