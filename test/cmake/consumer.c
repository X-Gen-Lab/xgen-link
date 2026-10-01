/**
 * \file            consumer.c
 * \brief           Link the public protocol API from a parent CMake project
 */

#include <xgl/xgl.h>

int main(void) {
    return xgl_version_int() != XGL_VERSION_INT || xgl_create(NULL) != NULL;
}
