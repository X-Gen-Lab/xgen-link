/**
 * \file            xgl_config.h
 * \brief           Compiled profile and protocol I/O bounds
 * \author          X-Gen Lab
 */

#ifndef XGL_CONFIG_H
#define XGL_CONFIG_H

#include <xgl/xgl_build_config.h>

/** \brief           Compiled byte limit for one synchronous PHY read. */
#ifndef XGL_DATALINK_RX_CHUNK_SIZE
#define XGL_DATALINK_RX_CHUNK_SIZE XGL_BUILD_RX_CHUNK_SIZE
#elif XGL_DATALINK_RX_CHUNK_SIZE != XGL_BUILD_RX_CHUNK_SIZE
#error                                                                         \
    "Configure XGL_DATALINK_RX_CHUNK_SIZE through CMake, not a consumer macro"
#endif

/** \brief           Absolute accepted serialized frame size, including CRC. */
#ifndef XGL_DATALINK_MAX_FRAME_SIZE
#define XGL_DATALINK_MAX_FRAME_SIZE 2048U
#endif

#endif /* XGL_CONFIG_H */
