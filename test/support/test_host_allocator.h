/**
 * \file            test_host_allocator.h
 * \brief           Explicit host storage for profile-independent fixtures
 * \author          X-Gen Lab
 */
#ifndef XGL_TEST_HOST_ALLOCATOR_H
#define XGL_TEST_HOST_ALLOCATOR_H

#include <cstdlib>
#include <xgl/xgl.h>

inline const xgl_allocator_t* xgl_test_host_allocator() {
    static const xgl_allocator_t service = {
        nullptr, [](void*, size_t size) -> void* { return std::malloc(size); },
        [](void*, void* address) { std::free(address); }};
    return &service;
}

/**
 * \brief           Supply host storage without overriding a fault-injection
 * backend.
 * \param[in,out]   config: Fixture-owned configuration that outlives its
 * instance.
 */
inline void xgl_test_use_host_allocator(xgl_config_t* config) {
    if (config->memory.allocator == nullptr) {
        config->memory.allocator = xgl_test_host_allocator();
    }
}

#endif
