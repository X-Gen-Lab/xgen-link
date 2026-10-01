/**
 * \file            xgl_config.h
 * \brief           Compiled profile and protocol I/O bounds
 * \author          X-Gen Lab
 */

#ifndef XGL_CONFIG_H
#define XGL_CONFIG_H

#include <xgl/xgl_build_config.h>

/** \brief           Maximum temporary PHY read chunk on the caller's stack. */
#ifndef XGL_DATALINK_RX_CHUNK_SIZE
#define XGL_DATALINK_RX_CHUNK_SIZE 128U
#endif

/** \brief           Absolute accepted serialized frame size, including CRC. */
#ifndef XGL_DATALINK_MAX_FRAME_SIZE
#define XGL_DATALINK_MAX_FRAME_SIZE 2048U
#endif

#endif /* XGL_CONFIG_H */
