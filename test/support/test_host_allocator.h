/**
 * \file            test_host_allocator.h
 * \brief           Explicit host storage for profile-independent fixtures
 * \author          X-Gen Lab
 */
#ifndef XGL_TEST_HOST_ALLOCATOR_H
#define XGL_TEST_HOST_ALLOCATOR_H

#include <cstdlib>
#include <xgl/xgl.h>
#include <xgl/xgl_alloc.h>

inline const xgl_allocator_t* xgl_test_host_allocator() {
    static const xgl_allocator_t service = {
        nullptr, [](void*, size_t size) -> void* { return std::malloc(size); },
        [](void*, void* address) { std::free(address); }};
    return &service;
}

/**
 * \brief           Start a fixture using an explicit host allocation backend.
 * \param[in]       config: Fixture-owned configuration that outlives the
 * instance.
 */
inline xgl_handle_t xgl_test_init_allocated(
    const xgl_config_t* config,
    const xgl_allocator_t* allocator = xgl_test_host_allocator()) {
    xgl_handle_t handle = nullptr;
    (void)xgl_init_allocated(config, allocator, &handle);
    return handle;
}

#endif
