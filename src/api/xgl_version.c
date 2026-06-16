/**
 * \file            xgl_version.c
 * \brief           Protocol version API implementation
 * \author          X-Gen Lab
 */

#include <xgl/xgl.h>

/*---------------------------------------------------------------------------*/
/* Version Information                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get protocol version string at runtime
 */
const char* xgl_version_string(void) {
    return XGL_VERSION_STRING;
}

/**
 * \brief           Get protocol version as integer at runtime
 */
uint32_t xgl_version_int(void) {
    return XGL_VERSION_INT;
}
