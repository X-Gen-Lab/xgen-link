/**
 * \file            xgl_allocator.c
 * \brief           Dispatch through an explicit XGL allocation service
 */

#include "internal/xgl_allocator.h"

#if XGL_ALLOW_FALLBACK_MALLOC
#include <stdlib.h>
#endif

bool xgl_allocator_is_valid(const xgl_allocator_t* allocator) {
    return allocator != NULL && allocator->alloc != NULL &&
           allocator->free != NULL;
}

void* xgl_alloc(const xgl_allocator_t* allocator, size_t size) {
    if (size == 0U || !xgl_allocator_is_valid(allocator)) {
        return NULL;
    }
    return allocator->alloc(allocator->ctx, size);
}

void xgl_free(const xgl_allocator_t* allocator, void* ptr) {
    if (ptr != NULL && xgl_allocator_is_valid(allocator)) {
        allocator->free(allocator->ctx, ptr);
    }
}

#if XGL_ALLOW_FALLBACK_MALLOC
static void* libc_alloc(void* ctx, size_t size) {
    (void)ctx;
    return malloc(size);
}

/* Fixed allocator callback parameter order. */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void libc_free(void* ctx, void* ptr) {
    (void)ctx;
    free(ptr);
}

const xgl_allocator_t* xgl_allocator_libc(void) {
    static const xgl_allocator_t allocator = {NULL, libc_alloc, libc_free};
    return &allocator;
}
#endif
