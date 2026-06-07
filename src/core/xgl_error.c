/**
 * \file            xgl_error.c
 * \brief           Error handling implementation
 * \author          Nexus Team
 */

#include "xgl/xgl_error.h"

/*---------------------------------------------------------------------------*/
/* Error String Lookup Table                                                */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Error code to string mapping
 */
static const struct {
    xgl_error_t code;
    const char* message;
} error_strings[] = {
    /* Success */
    {XGL_OK, "Success"},
    
    /* Parameter errors (1-99) */
    {XGL_ERR_INVALID_PARAM, "Invalid parameter"},
    {XGL_ERR_NULL_POINTER, "Null pointer"},
    {XGL_ERR_NOT_INITIALIZED, "Not initialized"},
    {XGL_ERR_ALREADY_INITIALIZED, "Already initialized"},
    
    /* Memory errors (100-199) */
    {XGL_ERR_NO_MEMORY, "Out of memory"},
    {XGL_ERR_POOL_EXHAUSTED, "Memory pool exhausted"},
    {XGL_ERR_BUFFER_TOO_SMALL, "Buffer too small"},
    
    /* Network errors (200-299) */
    {XGL_ERR_ROUTE_NOT_FOUND, "Route not found"},
    {XGL_ERR_TX_FAILED, "Transmission failed"},
    {XGL_ERR_TIMEOUT, "Operation timeout"},
    {XGL_ERR_ACK_TIMEOUT, "ACK timeout"},
    {XGL_ERR_TTL_EXPIRED, "TTL expired"},
    
    /* Protocol errors (300-399) */
    {XGL_ERR_INVALID_FRAME, "Invalid frame"},
    {XGL_ERR_CRC_FAILED, "CRC check failed"},
    {XGL_ERR_INVALID_VERSION, "Invalid version"},
    {XGL_ERR_INVALID_DATA_TYPE, "Invalid data type"},
    {XGL_ERR_SEQUENCE_ERROR, "Sequence number error"},
    
    /* State errors (400-499) */
    {XGL_ERR_BUSY, "Resource busy"},
    {XGL_ERR_QUEUE_FULL, "Queue full"},
    {XGL_ERR_WINDOW_FULL, "Sliding window full"},
};

/**
 * \brief           Number of error strings in the table
 */
#define ERROR_STRING_COUNT (sizeof(error_strings) / sizeof(error_strings[0]))

/*---------------------------------------------------------------------------*/
/* Public Functions                                                          */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Get error string description
 */
const char* xgl_error_string(xgl_error_t error) {
    /* Linear search through error table */
    for (size_t i = 0; i < ERROR_STRING_COUNT; i++) {
        if (error_strings[i].code == error) {
            return error_strings[i].message;
        }
    }
    
    /* Unknown error code */
    return "Unknown error";
}
