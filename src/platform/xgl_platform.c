/**
 * \file            xgl_platform.c
 * \brief           Platform detection implementation
 * \author          Nexus Team
 */

#include "xgl/xgl_platform.h"
#include <stdio.h>
#include <string.h>

/**
 * \brief           Print platform information to string
 */
int xgl_platform_info_string(char* buffer, size_t size) {
    if (buffer == NULL || size == 0) {
        return 0;
    }
    
    xgl_platform_info_t info;
    xgl_platform_get_info(&info);
    
    int written = 0;
    int ret;
    
    /* Compiler information */
    ret = snprintf(buffer + written, size - written,
                   "Compiler: %s (version %u)\n",
                   info.compiler_name, info.compiler_version);
    if (ret < 0 || (size_t)ret >= size - written) {
        return written;
    }
    written += ret;
    
    /* Operating system */
    ret = snprintf(buffer + written, size - written,
                   "OS: %s\n",
                   info.os_name);
    if (ret < 0 || (size_t)ret >= size - written) {
        return written;
    }
    written += ret;
    
    /* Architecture */
    if (info.arch_subname[0] != '\0') {
        ret = snprintf(buffer + written, size - written,
                       "Architecture: %s (%s)\n",
                       info.arch_name, info.arch_subname);
    } else {
        ret = snprintf(buffer + written, size - written,
                       "Architecture: %s\n",
                       info.arch_name);
    }
    if (ret < 0 || (size_t)ret >= size - written) {
        return written;
    }
    written += ret;
    
    /* Pointer size */
    ret = snprintf(buffer + written, size - written,
                   "Pointer Size: %u-bit\n",
                   info.pointer_size * 8);
    if (ret < 0 || (size_t)ret >= size - written) {
        return written;
    }
    written += ret;
    
    /* Endianness */
    ret = snprintf(buffer + written, size - written,
                   "Endianness: %s\n",
                   info.endian_name);
    if (ret < 0 || (size_t)ret >= size - written) {
        return written;
    }
    written += ret;
    
    /* Alignment */
    ret = snprintf(buffer + written, size - written,
                   "Alignment: %s (%u-byte required)\n",
                   info.alignment_name, info.alignment_required);
    if (ret < 0 || (size_t)ret >= size - written) {
        return written;
    }
    written += ret;
    
    return written;
}
