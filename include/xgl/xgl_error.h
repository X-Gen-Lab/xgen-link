/**
 * \file            xgl_error.h
 * \brief           xgen-link Protocol Error Codes and Handling
 * \author          Nexus Team
 */

#ifndef XGL_ERROR_H
#define XGL_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * \brief           Protocol error codes
 */
typedef enum {
    XGL_OK = 0,                     /**< Success */
    
    /*-----------------------------------------------------------------------*/
    /* Parameter errors (1-99)                                               */
    /*-----------------------------------------------------------------------*/
    XGL_ERR_INVALID_PARAM = 1,      /**< Invalid parameter */
    XGL_ERR_NULL_POINTER = 2,       /**< Null pointer */
    XGL_ERR_NOT_INITIALIZED = 3,    /**< Not initialized */
    XGL_ERR_ALREADY_INITIALIZED = 4,/**< Already initialized */
    
    /*-----------------------------------------------------------------------*/
    /* Memory errors (100-199)                                               */
    /*-----------------------------------------------------------------------*/
    XGL_ERR_NO_MEMORY = 100,        /**< Out of memory */
    XGL_ERR_POOL_EXHAUSTED = 101,   /**< Memory pool exhausted */
    XGL_ERR_BUFFER_TOO_SMALL = 102, /**< Buffer too small */
    
    /*-----------------------------------------------------------------------*/
    /* Network errors (200-299)                                              */
    /*-----------------------------------------------------------------------*/
    XGL_ERR_ROUTE_NOT_FOUND = 200,  /**< Route not found */
    XGL_ERR_TX_FAILED = 201,        /**< Transmission failed */
    XGL_ERR_TIMEOUT = 202,          /**< Operation timeout */
    XGL_ERR_ACK_TIMEOUT = 203,      /**< ACK timeout */
    
    /*-----------------------------------------------------------------------*/
    /* Protocol errors (300-399)                                             */
    /*-----------------------------------------------------------------------*/
    XGL_ERR_INVALID_FRAME = 300,    /**< Invalid frame */
    XGL_ERR_CRC_FAILED = 301,       /**< CRC check failed */
    XGL_ERR_INVALID_VERSION = 302,  /**< Invalid version */
    XGL_ERR_INVALID_DATA_TYPE = 303,/**< Invalid data type */
    XGL_ERR_SEQUENCE_ERROR = 304,   /**< Sequence number error */
    
    /*-----------------------------------------------------------------------*/
    /* State errors (400-499)                                                */
    /*-----------------------------------------------------------------------*/
    XGL_ERR_BUSY = 400,             /**< Resource busy */
    XGL_ERR_QUEUE_FULL = 401,       /**< Queue full */
    XGL_ERR_WINDOW_FULL = 402,      /**< Sliding window full */
    
} xgl_error_t;

/**
 * \brief           Get error string description
 * \param[in]       error: Error code
 * \return          Error description string (never NULL)
 */
const char* xgl_error_string(xgl_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* XGL_ERROR_H */
