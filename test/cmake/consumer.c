/**
 * \file            consumer.c
 * \brief           Link the public protocol API from a parent CMake project
 */

#include <xgl/xgl.h>

int main(void) {
    return xgl_version_int() != 30000U || xgl_create(NULL) != NULL;
}
