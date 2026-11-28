/**
 * \file            xgl_version.c
 * \brief           SDK version queries
 * \author          X-Gen Lab
 * \version         0.0.1
 * \date            2026-06-16
 * \copyright       Copyright (c) 2026 X-Gen Lab
 */

#include <xgl/xgl.h>

/*---------------------------------------------------------------------------*/
/* Version Information                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get the linked SDK version string
 */
const char* xgl_version_string(void) {
    return XGL_VERSION_STRING;
}

/**
 * \brief           Get the linked SDK version as an integer
 */
uint32_t xgl_version_int(void) {
    return XGL_VERSION_INT;
}
