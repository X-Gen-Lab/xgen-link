/**
 * \file            test_host_allocator.h
 * \brief           Explicit host storage for profile-independent fixtures
 * \author          X-Gen Lab
 */
#ifndef XGL_TEST_HOST_ALLOCATOR_H
#define XGL_TEST_HOST_ALLOCATOR_H

#include <xgen/memory/libc_allocator.h>
#include <xgl/xgl.h>

/**
 * \brief           Supply host storage without overriding a fault-injection
 * backend.
 * \param[in,out]   config: Fixture-owned configuration that outlives its
 * instance.
 */
inline void xgl_test_use_host_allocator(xgl_config_t* config) {
    if (config->memory.allocator == nullptr) {
        config->memory.allocator = xgm_allocator_libc();
    }
}

#endif
