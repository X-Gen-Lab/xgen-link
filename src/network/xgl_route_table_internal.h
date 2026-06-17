/**
 * \file            xgl_route_table_internal.h
 * \brief           Route table private helpers
 * \author          X-Gen Lab
 */

#ifndef XGL_ROUTE_TABLE_INTERNAL_H
#define XGL_ROUTE_TABLE_INTERNAL_H

#include <stddef.h>
#include <xgl/internal/xgl_route.h>

void* xgl_route_table_alloc(xgl_route_table_t* table, size_t size);
void xgl_route_table_free(xgl_route_table_t* table, void* ptr);

#endif /* XGL_ROUTE_TABLE_INTERNAL_H */
