/**
 * \file            xgl_config.h
 * \brief           Configuration constants and tunable parameters
 * \author          X-Gen Lab
 */

#ifndef XGL_CONFIG_H
#define XGL_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------------------*/
/* Data Link Layer Configuration                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Stack buffer size for frame transmission
 * \details         Frames larger than this will be allocated from heap.
 *                  Recommended: 512 bytes (covers most small/medium frames)
 *                  - Tiny systems: 256 bytes
 *                  - Small systems: 512 bytes
 *                  - Medium/Large systems: 1024 bytes
 */
#ifndef XGL_DATALINK_STACK_BUFFER_SIZE
#define XGL_DATALINK_STACK_BUFFER_SIZE      512
#endif

/**
 * \brief           RX chunk size for reading from physical layer
 * \details         Size of temporary buffer for receiving data chunks.
 *                  Smaller values reduce stack usage but may require more calls.
 *                  Recommended: 128 bytes
 *                  - Tiny systems: 64 bytes
 *                  - Small systems: 128 bytes
 *                  - Medium/Large systems: 256 bytes
 */
#ifndef XGL_DATALINK_RX_CHUNK_SIZE
#define XGL_DATALINK_RX_CHUNK_SIZE          128
#endif

/**
 * \brief           Maximum allowed frame size for security validation
 * \details         Absolute maximum frame size to prevent buffer overflow attacks.
 *                  Should be larger than any configured max_frame_size in routes.
 *                  Recommended: 2048 bytes
 */
#ifndef XGL_DATALINK_MAX_FRAME_SIZE
#define XGL_DATALINK_MAX_FRAME_SIZE         2048
#endif

/*---------------------------------------------------------------------------*/
/* Transport Layer Configuration                                             */
/*---------------------------------------------------------------------------*/

/**
 * \brief           ACK buffer size
 * \details         Size of buffer for ACK frame generation.
 *                  ACK frames are header + CRC16 = 14 bytes.
 *                  Recommended: 32 bytes (provides safety margin)
 */
#ifndef XGL_ACK_BUFFER_SIZE
#define XGL_ACK_BUFFER_SIZE                 32
#endif

/**
 * \brief           Default ACK timeout in milliseconds
 */
#ifndef XGL_DEFAULT_ACK_TIMEOUT_MS
#define XGL_DEFAULT_ACK_TIMEOUT_MS          1000
#endif

/**
 * \brief           Default maximum retry count
 */
#ifndef XGL_DEFAULT_MAX_RETRY_COUNT
#define XGL_DEFAULT_MAX_RETRY_COUNT         5
#endif

/**
 * \brief           Default sliding window size
 */
#ifndef XGL_DEFAULT_WINDOW_SIZE
#define XGL_DEFAULT_WINDOW_SIZE             4
#endif

/*---------------------------------------------------------------------------*/
/* Network Layer Configuration                                               */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default routing table size
 */
#ifndef XGL_DEFAULT_ROUTE_TABLE_SIZE
#define XGL_DEFAULT_ROUTE_TABLE_SIZE        8
#endif

/*---------------------------------------------------------------------------*/
/* Memory Pool Configuration                                                 */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Default TX pool size in bytes
 */
#ifndef XGL_DEFAULT_TX_POOL_SIZE
#define XGL_DEFAULT_TX_POOL_SIZE            2048
#endif

/**
 * \brief           Default RX buffer size in bytes
 */
#ifndef XGL_DEFAULT_RX_BUFFER_SIZE
#define XGL_DEFAULT_RX_BUFFER_SIZE          288
#endif

#ifdef __cplusplus
}
#endif

#endif /* XGL_CONFIG_H */
