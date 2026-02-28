/**
 * \file            xgl_serialize.h
 * \brief           Serialization and deserialization utilities
 * \author          Nexus Team
 */

#ifndef XGL_SERIALIZE_H
#define XGL_SERIALIZE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*---------------------------------------------------------------------------*/
/* Endianness Detection                                                      */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Detect host endianness at compile time
 * \details         Returns true if host is little-endian, false if big-endian
 */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define XGL_IS_LITTLE_ENDIAN 1
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define XGL_IS_LITTLE_ENDIAN 0
#elif defined(_WIN32) || defined(_WIN64)
#define XGL_IS_LITTLE_ENDIAN 1  /* Windows is always little-endian */
#elif defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || \
      defined(__AARCH64EL__) || defined(_MIPSEL) || defined(__MIPSEL) || \
      defined(__MIPSEL__)
#define XGL_IS_LITTLE_ENDIAN 1
#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || \
      defined(__AARCH64EB__) || defined(_MIPSEB) || defined(__MIPSEB) || \
      defined(__MIPSEB__)
#define XGL_IS_LITTLE_ENDIAN 0
#else
/* Default to little-endian for unknown platforms */
#define XGL_IS_LITTLE_ENDIAN 1
#endif

/*---------------------------------------------------------------------------*/
/* Alignment Detection                                                       */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Detect if platform requires strict alignment
 * \details         ARM Cortex-M0/M0+ require strict alignment
 */
#if defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_6SM__)
#define XGL_STRICT_ALIGNMENT 1  /* ARM Cortex-M0/M0+ */
#elif defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__)
#define XGL_STRICT_ALIGNMENT 0  /* ARM Cortex-M3/M4 support unaligned access */
#elif defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#define XGL_STRICT_ALIGNMENT 0  /* x86/x64 support unaligned access */
#else
/* Default to strict alignment for safety */
#define XGL_STRICT_ALIGNMENT 1
#endif

/*---------------------------------------------------------------------------*/
/* Little-Endian Serialization (uint16_t)                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Serialize uint16_t to little-endian byte order
 * \param[out]      buffer: Output buffer (must have at least 2 bytes)
 * \param[in]       value: Value to serialize
 */
void xgl_serialize_u16_le(uint8_t* buffer, uint16_t value);

/**
 * \brief           Deserialize uint16_t from little-endian byte order
 * \param[in]       buffer: Input buffer (must have at least 2 bytes)
 * \return          Deserialized value
 */
uint16_t xgl_deserialize_u16_le(const uint8_t* buffer);

/*---------------------------------------------------------------------------*/
/* Little-Endian Serialization (uint32_t)                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Serialize uint32_t to little-endian byte order
 * \param[out]      buffer: Output buffer (must have at least 4 bytes)
 * \param[in]       value: Value to serialize
 */
void xgl_serialize_u32_le(uint8_t* buffer, uint32_t value);

/**
 * \brief           Deserialize uint32_t from little-endian byte order
 * \param[in]       buffer: Input buffer (must have at least 4 bytes)
 * \return          Deserialized value
 */
uint32_t xgl_deserialize_u32_le(const uint8_t* buffer);

/*---------------------------------------------------------------------------*/
/* Alignment-Safe Accessor Macros                                           */
/*---------------------------------------------------------------------------*/

#if XGL_STRICT_ALIGNMENT

/**
 * \brief           Read uint16_t from potentially unaligned buffer (safe)
 * \param[in]       ptr: Pointer to buffer
 * \return          Value read from buffer
 */
#define XGL_READ_U16(ptr) xgl_deserialize_u16_le((const uint8_t*)(ptr))

/**
 * \brief           Write uint16_t to potentially unaligned buffer (safe)
 * \param[out]      ptr: Pointer to buffer
 * \param[in]       val: Value to write
 */
#define XGL_WRITE_U16(ptr, val) xgl_serialize_u16_le((uint8_t*)(ptr), (val))

/**
 * \brief           Read uint32_t from potentially unaligned buffer (safe)
 * \param[in]       ptr: Pointer to buffer
 * \return          Value read from buffer
 */
#define XGL_READ_U32(ptr) xgl_deserialize_u32_le((const uint8_t*)(ptr))

/**
 * \brief           Write uint32_t to potentially unaligned buffer (safe)
 * \param[out]      ptr: Pointer to buffer
 * \param[in]       val: Value to write
 */
#define XGL_WRITE_U32(ptr, val) xgl_serialize_u32_le((uint8_t*)(ptr), (val))

#else

/**
 * \brief           Read uint16_t from buffer (direct access on platforms supporting unaligned access)
 * \param[in]       ptr: Pointer to buffer
 * \return          Value read from buffer
 */
#define XGL_READ_U16(ptr) (*(const uint16_t*)(ptr))

/**
 * \brief           Write uint16_t to buffer (direct access on platforms supporting unaligned access)
 * \param[out]      ptr: Pointer to buffer
 * \param[in]       val: Value to write
 */
#define XGL_WRITE_U16(ptr, val) (*(uint16_t*)(ptr) = (val))

/**
 * \brief           Read uint32_t from buffer (direct access on platforms supporting unaligned access)
 * \param[in]       ptr: Pointer to buffer
 * \return          Value read from buffer
 */
#define XGL_READ_U32(ptr) (*(const uint32_t*)(ptr))

/**
 * \brief           Write uint32_t to buffer (direct access on platforms supporting unaligned access)
 * \param[out]      ptr: Pointer to buffer
 * \param[in]       val: Value to write
 */
#define XGL_WRITE_U32(ptr, val) (*(uint32_t*)(ptr) = (val))

#endif

/*---------------------------------------------------------------------------*/
/* Endianness Conversion Macros                                             */
/*---------------------------------------------------------------------------*/

#if XGL_IS_LITTLE_ENDIAN

/**
 * \brief           Convert uint16_t from host to little-endian (no-op on LE)
 */
#define XGL_HTOLE16(x) (x)

/**
 * \brief           Convert uint16_t from little-endian to host (no-op on LE)
 */
#define XGL_LE16TOH(x) (x)

/**
 * \brief           Convert uint32_t from host to little-endian (no-op on LE)
 */
#define XGL_HTOLE32(x) (x)

/**
 * \brief           Convert uint32_t from little-endian to host (no-op on LE)
 */
#define XGL_LE32TOH(x) (x)

#else

/**
 * \brief           Swap bytes in uint16_t
 */
#define XGL_BSWAP16(x) ((uint16_t)(((x) >> 8) | ((x) << 8)))

/**
 * \brief           Swap bytes in uint32_t
 */
#define XGL_BSWAP32(x) ((uint32_t)(((x) >> 24) | \
                                   (((x) & 0x00FF0000) >> 8) | \
                                   (((x) & 0x0000FF00) << 8) | \
                                   ((x) << 24)))

/**
 * \brief           Convert uint16_t from host to little-endian (swap on BE)
 */
#define XGL_HTOLE16(x) XGL_BSWAP16(x)

/**
 * \brief           Convert uint16_t from little-endian to host (swap on BE)
 */
#define XGL_LE16TOH(x) XGL_BSWAP16(x)

/**
 * \brief           Convert uint32_t from host to little-endian (swap on BE)
 */
#define XGL_HTOLE32(x) XGL_BSWAP32(x)

/**
 * \brief           Convert uint32_t from little-endian to host (swap on BE)
 */
#define XGL_LE32TOH(x) XGL_BSWAP32(x)

#endif

#ifdef __cplusplus
}
#endif

#endif /* XGL_SERIALIZE_H */
