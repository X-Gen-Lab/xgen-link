/**
 * \file            xgl_log.c
 * \brief           Logging framework implementation
 * \author          X-Gen Lab
 */

#ifdef XGL_ENABLE_LOGGING

#include <stdarg.h>
#include <stdio.h>
#include "xgl/internal/xgl_log.h"

/*---------------------------------------------------------------------------*/
/* Module State                                                              */
/*---------------------------------------------------------------------------*/

static xgl_log_callback_t g_log_callback;
static xgl_log_level_t g_log_level = XGL_LOG_LEVEL_INFO;
static void* g_log_user_data;

/*---------------------------------------------------------------------------*/
/* Initialization                                                            */
/*---------------------------------------------------------------------------*/

void xgl_log_init(xgl_log_callback_t callback,
                  xgl_log_level_t level,
                  void* user_data)
{
    g_log_callback = callback;
    g_log_level = level;
    g_log_user_data = user_data;
}

/*---------------------------------------------------------------------------*/
/* Log Output                                                                */
/*---------------------------------------------------------------------------*/

void xgl_log_output(xgl_log_level_t level,
                    const char* tag,
                    const char* fmt,
                    ...)
{
    if (level == XGL_LOG_LEVEL_NONE || level > g_log_level) {
        return;
    }

    if (g_log_callback == NULL) {
        return;
    }

    /* Format message into stack buffer */
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    /* Ensure null-termination */
    buffer[sizeof(buffer) - 1] = '\0';

    g_log_callback(level, tag, buffer, g_log_user_data);
}

#endif /* XGL_ENABLE_LOGGING */

/* Prevent empty translation unit warning when logging is disabled */
typedef int xgl_log_translation_unit_not_empty_;
