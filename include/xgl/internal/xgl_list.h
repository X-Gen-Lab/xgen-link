/**
 * \file            xgl_list.h
 * \brief           Intrusive doubly-linked list data structure
 * \author          X-Gen Lab
 */

#ifndef XGL_LIST_H
#define XGL_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------*/
/* List Node Structure                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Intrusive list node
 * \details         Embed this structure in your data structure to make it
 *                  linkable in a list
 */
typedef struct xgl_list_node {
    struct xgl_list_node* next;     /**< Pointer to next node */
    struct xgl_list_node* prev;     /**< Pointer to previous node */
} xgl_list_node_t;

/**
 * \brief           List head structure
 */
typedef struct {
    xgl_list_node_t* head;          /**< Pointer to first node */
    xgl_list_node_t* tail;          /**< Pointer to last node */
    size_t count;                   /**< Number of nodes in list */
} xgl_list_t;

/*---------------------------------------------------------------------------*/
/* List Initialization                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Initialize a list
 * \param[in,out]   list: Pointer to list structure
 */
void xgl_list_init(xgl_list_t* list);

/**
 * \brief           Initialize a list node
 * \param[in,out]   node: Pointer to list node
 */
void xgl_list_node_init(xgl_list_node_t* node);

/*---------------------------------------------------------------------------*/
/* List Operations                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Check if list is empty
 * \param[in]       list: Pointer to list structure
 * \return          true if list is empty, false otherwise
 */
bool xgl_list_is_empty(const xgl_list_t* list);

/**
 * \brief           Get number of nodes in list
 * \param[in]       list: Pointer to list structure
 * \return          Number of nodes in list
 */
size_t xgl_list_count(const xgl_list_t* list);

/**
 * \brief           Insert node at head of list
 * \param[in,out]   list: Pointer to list structure
 * \param[in]       node: Pointer to node to insert
 */
void xgl_list_insert_head(xgl_list_t* list, xgl_list_node_t* node);

/**
 * \brief           Insert node at tail of list
 * \param[in,out]   list: Pointer to list structure
 * \param[in]       node: Pointer to node to insert
 */
void xgl_list_insert_tail(xgl_list_t* list, xgl_list_node_t* node);

/**
 * \brief           Insert node after specified node
 * \param[in,out]   list: Pointer to list structure
 * \param[in]       pos: Pointer to position node
 * \param[in]       node: Pointer to node to insert
 */
void xgl_list_insert_after(xgl_list_t* list, xgl_list_node_t* pos,
                          xgl_list_node_t* node);

/**
 * \brief           Insert node before specified node
 * \param[in,out]   list: Pointer to list structure
 * \param[in]       pos: Pointer to position node
 * \param[in]       node: Pointer to node to insert
 */
void xgl_list_insert_before(xgl_list_t* list, xgl_list_node_t* pos,
                           xgl_list_node_t* node);

/**
 * \brief           Remove node from list
 * \param[in,out]   list: Pointer to list structure
 * \param[in]       node: Pointer to node to remove
 */
void xgl_list_remove(xgl_list_t* list, xgl_list_node_t* node);

/**
 * \brief           Remove and return head node
 * \param[in,out]   list: Pointer to list structure
 * \return          Pointer to removed node, NULL if list is empty
 */
xgl_list_node_t* xgl_list_remove_head(xgl_list_t* list);

/**
 * \brief           Remove and return tail node
 * \param[in,out]   list: Pointer to list structure
 * \return          Pointer to removed node, NULL if list is empty
 */
xgl_list_node_t* xgl_list_remove_tail(xgl_list_t* list);

/**
 * \brief           Get head node without removing
 * \param[in]       list: Pointer to list structure
 * \return          Pointer to head node, NULL if list is empty
 */
xgl_list_node_t* xgl_list_peek_head(const xgl_list_t* list);

/**
 * \brief           Get tail node without removing
 * \param[in]       list: Pointer to list structure
 * \return          Pointer to tail node, NULL if list is empty
 */
xgl_list_node_t* xgl_list_peek_tail(const xgl_list_t* list);

/**
 * \brief           Get next node in list
 * \param[in]       node: Pointer to current node
 * \return          Pointer to next node, NULL if at end
 */
xgl_list_node_t* xgl_list_next(const xgl_list_node_t* node);

/**
 * \brief           Get previous node in list
 * \param[in]       node: Pointer to current node
 * \return          Pointer to previous node, NULL if at beginning
 */
xgl_list_node_t* xgl_list_prev(const xgl_list_node_t* node);

/*---------------------------------------------------------------------------*/
/* Container Access Macro                                                    */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get pointer to containing structure
 * \param[in]       ptr: Pointer to member
 * \param[in]       type: Type of containing structure
 * \param[in]       member: Name of member within structure
 * \return          Pointer to containing structure
 */
#define XGL_LIST_ENTRY(ptr, type, member) \
    ((type*)(void*)((char*)(ptr) - offsetof(type, member)))

/**
 * \brief           Iterate over list
 * \param[in]       list: Pointer to list structure
 * \param[in]       node: Iterator variable (xgl_list_node_t*)
 */
#define XGL_LIST_FOR_EACH(list, node) \
    for ((node) = (list)->head; (node) != NULL; (node) = (node)->next)

/**
 * \brief           Iterate over list safely (allows removal during iteration)
 * \param[in]       list: Pointer to list structure
 * \param[in]       node: Iterator variable (xgl_list_node_t*)
 * \param[in]       tmp: Temporary variable (xgl_list_node_t*)
 */
#define XGL_LIST_FOR_EACH_SAFE(list, node, tmp) \
    for ((node) = (list)->head, (tmp) = ((node) ? (node)->next : NULL); \
         (node) != NULL; \
         (node) = (tmp), (tmp) = ((node) ? (node)->next : NULL))

/*---------------------------------------------------------------------------*/
/* Thread-Safe Variants (if XGL_THREAD_SAFE enabled)                        */
/*---------------------------------------------------------------------------*/

#ifdef XGL_THREAD_SAFE

#include "xgl/internal/xgl_mutex.h"

/**
 * \brief           Thread-safe list structure
 */
typedef struct {
    xgl_list_t list;                /**< Underlying list */
    xgl_mutex_t mutex;              /**< Mutex for thread safety */
} xgl_list_ts_t;

/**
 * \brief           Initialize thread-safe list
 * \param[in,out]   list: Pointer to thread-safe list structure
 * \return          0 on success, error code otherwise
 */
int xgl_list_ts_init(xgl_list_ts_t* list);

/**
 * \brief           Destroy thread-safe list
 * \param[in,out]   list: Pointer to thread-safe list structure
 */
void xgl_list_ts_destroy(xgl_list_ts_t* list);

/**
 * \brief           Check if thread-safe list is empty
 * \param[in]       list: Pointer to thread-safe list structure
 * \return          true if list is empty, false otherwise
 */
bool xgl_list_ts_is_empty(xgl_list_ts_t* list);

/**
 * \brief           Get number of nodes in thread-safe list
 * \param[in]       list: Pointer to thread-safe list structure
 * \return          Number of nodes in list
 */
size_t xgl_list_ts_count(xgl_list_ts_t* list);

/**
 * \brief           Insert node at head of thread-safe list
 * \param[in,out]   list: Pointer to thread-safe list structure
 * \param[in]       node: Pointer to node to insert
 */
void xgl_list_ts_insert_head(xgl_list_ts_t* list, xgl_list_node_t* node);

/**
 * \brief           Insert node at tail of thread-safe list
 * \param[in,out]   list: Pointer to thread-safe list structure
 * \param[in]       node: Pointer to node to insert
 */
void xgl_list_ts_insert_tail(xgl_list_ts_t* list, xgl_list_node_t* node);

/**
 * \brief           Remove node from thread-safe list
 * \param[in,out]   list: Pointer to thread-safe list structure
 * \param[in]       node: Pointer to node to remove
 */
void xgl_list_ts_remove(xgl_list_ts_t* list, xgl_list_node_t* node);

/**
 * \brief           Remove and return head node from thread-safe list
 * \param[in,out]   list: Pointer to thread-safe list structure
 * \return          Pointer to removed node, NULL if list is empty
 */
xgl_list_node_t* xgl_list_ts_remove_head(xgl_list_ts_t* list);

/**
 * \brief           Remove and return tail node from thread-safe list
 * \param[in,out]   list: Pointer to thread-safe list structure
 * \return          Pointer to removed node, NULL if list is empty
 */
xgl_list_node_t* xgl_list_ts_remove_tail(xgl_list_ts_t* list);

/**
 * \brief           Get head node without removing from thread-safe list
 * \param[in]       list: Pointer to thread-safe list structure
 * \return          Pointer to head node, NULL if list is empty
 */
xgl_list_node_t* xgl_list_ts_peek_head(xgl_list_ts_t* list);

/**
 * \brief           Get tail node without removing from thread-safe list
 * \param[in]       list: Pointer to thread-safe list structure
 * \return          Pointer to tail node, NULL if list is empty
 */
xgl_list_node_t* xgl_list_ts_peek_tail(xgl_list_ts_t* list);

#endif /* XGL_THREAD_SAFE */

#ifdef __cplusplus
}
#endif

#endif /* XGL_LIST_H */
