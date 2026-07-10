/**
 * \file            xgl_log.h
 * \brief           Logging framework macros and interface
 * \author          X-Gen Lab
 */

#ifndef XGL_LOG_H
#define XGL_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/*---------------------------------------------------------------------------*/
/* Log Level Definitions                                                     */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Log levels (higher value = more verbose)
 */
typedef enum {
    XGL_LOG_LEVEL_NONE    = 0,  /**< No logging */
    XGL_LOG_LEVEL_ERROR   = 1,  /**< Error messages */
    XGL_LOG_LEVEL_WARNING = 2,  /**< Warning messages */
    XGL_LOG_LEVEL_INFO    = 3,  /**< Informational messages */
    XGL_LOG_LEVEL_DEBUG   = 4,  /**< Debug messages */
    XGL_LOG_LEVEL_VERBOSE = 5   /**< Verbose detail messages */
} xgl_log_level_t;

/*---------------------------------------------------------------------------*/
/* Log Callback Type                                                         */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Log output callback function type
 * \param[in]       level: Log level of the message
 * \param[in]       tag: Component tag (e.g., "transport", "datalink")
 * \param[in]       message: Formatted log message string
 * \param[in]       user_data: User data from configuration
 */
typedef void (*xgl_log_callback_t)(xgl_log_level_t level,
                                   const char* tag,
                                   const char* message,
                                   void* user_data);

/*---------------------------------------------------------------------------*/
/* Log Output Function                                                       */
/*---------------------------------------------------------------------------*/

#ifdef XGL_ENABLE_LOGGING

/**
 * \brief           Output a log message (internal; call via macros)
 * \param[in]       level: Log level
 * \param[in]       tag: Component tag
 * \param[in]       fmt: printf-style format string
 * \param[in]       ...: Format arguments
 */
void xgl_log_output(xgl_log_level_t level,
                    const char* tag,
                    const char* fmt,
                    ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)))
#endif
    ;

/**
 * \brief           Initialize the logging subsystem
 * \param[in]       callback: User log callback (may be NULL for no output)
 * \param[in]       level: Minimum log level to output
 * \param[in]       user_data: User data passed to callback
 */
void xgl_log_init(xgl_log_callback_t callback,
                  xgl_log_level_t level,
                  void* user_data);

/*---------------------------------------------------------------------------*/
/* Logging Macros                                                            */
/*---------------------------------------------------------------------------*/

#define XGL_LOG_ERROR(tag, fmt, ...) \
    xgl_log_output(XGL_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define XGL_LOG_WARN(tag, fmt, ...) \
    xgl_log_output(XGL_LOG_LEVEL_WARNING, tag, fmt, ##__VA_ARGS__)
#define XGL_LOG_INFO(tag, fmt, ...) \
    xgl_log_output(XGL_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define XGL_LOG_DEBUG(tag, fmt, ...) \
    xgl_log_output(XGL_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define XGL_LOG_VERBOSE(tag, fmt, ...) \
    xgl_log_output(XGL_LOG_LEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)

#else /* !XGL_ENABLE_LOGGING */

/* Compile-time zero-cost stubs when logging is disabled */
#define XGL_LOG_ERROR(tag, fmt, ...)   ((void)0)
#define XGL_LOG_WARN(tag, fmt, ...)    ((void)0)
#define XGL_LOG_INFO(tag, fmt, ...)    ((void)0)
#define XGL_LOG_DEBUG(tag, fmt, ...)   ((void)0)
#define XGL_LOG_VERBOSE(tag, fmt, ...) ((void)0)

/* No-op init when logging disabled */
#define xgl_log_init(cb, lvl, ud)      ((void)0)

#endif /* XGL_ENABLE_LOGGING */

#ifdef __cplusplus
}
#endif

#endif /* XGL_LOG_H */
