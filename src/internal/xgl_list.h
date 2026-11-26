/**
 * \file            xgl_list.h
 * \brief           Intrusive lists for protocol-owned records
 * \author          X-Gen Lab
 */

#ifndef XGL_LIST_H
#define XGL_LIST_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xgl_list_node {
    struct xgl_list_node* next;
    struct xgl_list_node* prev;
} xgl_list_node_t;

typedef struct {
    xgl_list_node_t* head;
    xgl_list_node_t* tail;
    size_t count;
} xgl_list_t;

/* Lists borrow their nodes. Callers serialize changes, insert only unlinked
 * nodes and keep both the owning records and their lists at stable addresses.
 */
void xgl_list_init(xgl_list_t* list);
void xgl_list_node_init(xgl_list_node_t* node);
bool xgl_list_is_empty(const xgl_list_t* list);
size_t xgl_list_count(const xgl_list_t* list);
void xgl_list_insert_tail(xgl_list_t* list, xgl_list_node_t* node);
void xgl_list_remove(xgl_list_t* list, xgl_list_node_t* node);
xgl_list_node_t* xgl_list_remove_head(xgl_list_t* list);

/* Recover the owning record from its actual embedded node. */
#define XGL_LIST_ENTRY(ptr, type, member)                                      \
    ((type*)(void*)((char*)(ptr) - offsetof(type, member)))

#define XGL_LIST_FOR_EACH(list, node)                                          \
    for ((node) = (list)->head; (node) != NULL; (node) = (node)->next)

/* The safe form permits removal of the current node only. */
#define XGL_LIST_FOR_EACH_SAFE(list, node, tmp)                                \
    for ((node) = (list)->head, (tmp) = ((node) ? (node)->next : NULL);        \
         (node) != NULL;                                                       \
         (node) = (tmp), (tmp) = ((node) ? (node)->next : NULL))

#ifdef __cplusplus
}
#endif

#endif /* XGL_LIST_H */
