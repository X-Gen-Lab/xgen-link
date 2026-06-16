/**
 * \file            xgl_platform.c
 * \brief           Platform detection implementation
 * \author          X-Gen Lab
 */

#include "xgl/internal/xgl_platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int append_platform_info(char* buffer,
                                size_t size,
                                size_t* written,
                                const char* format,
                                ...) {
    if (*written >= size) {
        return 0;
    }

    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer + *written, size - *written, format, args);
    va_end(args);

    if (ret < 0) {
        return -1;
    }

    size_t remaining = size - *written;
    if ((size_t)ret >= remaining) {
        *written = size - 1U;
        return 1;
    }

    *written += (size_t)ret;
    return 0;
}

/**
 * \brief           Print platform information to string
 */
int xgl_platform_info_string(char* buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return 0;
    }

    xgl_platform_info_t info;
    xgl_platform_get_info(&info);

    size_t written = 0;
    int ret;

    /* Compiler information */
    ret = append_platform_info(buffer, size, &written,
                               "Compiler: %s (version %u)\n",
                               info.compiler_name, info.compiler_version);
    if (ret != 0) {
        return (int)written;
    }

    /* Operating system */
    ret = append_platform_info(buffer, size, &written,
                               "OS: %s\n",
                               info.os_name);
    if (ret != 0) {
        return (int)written;
    }

    /* Architecture */
    if (info.arch_subname[0] != '\0') {
        ret = append_platform_info(buffer, size, &written,
                                   "Architecture: %s (%s)\n",
                                   info.arch_name, info.arch_subname);
    } else {
        ret = append_platform_info(buffer, size, &written,
                                   "Architecture: %s\n",
                                   info.arch_name);
    }
    if (ret != 0) {
        return (int)written;
    }

    /* Pointer size */
    ret = append_platform_info(buffer, size, &written,
                               "Pointer Size: %u-bit\n",
                               info.pointer_size * 8U);
    if (ret != 0) {
        return (int)written;
    }

    /* Endianness */
    ret = append_platform_info(buffer, size, &written,
                               "Endianness: %s\n",
                               info.endian_name);
    if (ret != 0) {
        return (int)written;
    }

    /* Alignment */
    ret = append_platform_info(buffer, size, &written,
                               "Alignment: %s (%u-byte required)\n",
                               info.alignment_name, info.alignment_required);
    if (ret != 0) {
        return (int)written;
    }

    return (int)written;
}
