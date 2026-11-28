/**
 * \file            xgl_version.c
 * \brief           SDK version queries
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-06-16
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <xgl/xgl.h>
#include <xgl/xgl_diagnostics.h>

/**
 * \brief           Get the linked SDK version as an integer
 */
uint32_t xgl_version_int(void) {
    return XGL_VERSION_INT;
}
