/**
 * \file            xgl_wire_bytes.h
 * \brief           Fixed-width byte access for wire fields and nonces
 */

#ifndef XGL_WIRE_BYTES_H
#define XGL_WIRE_BYTES_H

#include <stdint.h>

/** \brief           Write a little-endian value to a validated 2-byte span. */
static inline void xgl_serialize_u16_le(uint8_t* buffer, uint16_t value) {
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8U);
}

/** \brief           Read a little-endian value from a validated 2-byte span. */
static inline uint16_t xgl_deserialize_u16_le(const uint8_t* buffer) {
    return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U));
}

/** \brief           Write a little-endian value to a validated 4-byte span. */
static inline void xgl_serialize_u32_le(uint8_t* buffer, uint32_t value) {
    buffer[0] = (uint8_t)value;
    buffer[1] = (uint8_t)(value >> 8U);
    buffer[2] = (uint8_t)(value >> 16U);
    buffer[3] = (uint8_t)(value >> 24U);
}

/** \brief           Read a little-endian value from a validated 4-byte span. */
static inline uint32_t xgl_deserialize_u32_le(const uint8_t* buffer) {
    return (uint32_t)buffer[0] | ((uint32_t)buffer[1] << 8U) |
           ((uint32_t)buffer[2] << 16U) | ((uint32_t)buffer[3] << 24U);
}

/** \brief           Write a little-endian value to a validated 8-byte span. */
static inline void xgl_serialize_u64_le(uint8_t* buffer, uint64_t value) {
    xgl_serialize_u32_le(buffer, (uint32_t)value);
    xgl_serialize_u32_le(buffer + 4U, (uint32_t)(value >> 32U));
}

/** \brief           Read a little-endian value from a validated 8-byte span. */
static inline uint64_t xgl_deserialize_u64_le(const uint8_t* buffer) {
    return (uint64_t)xgl_deserialize_u32_le(buffer) |
           ((uint64_t)xgl_deserialize_u32_le(buffer + 4U) << 32U);
}

/** \brief           Write a big-endian value to a validated 4-byte span. */
static inline void xgl_serialize_u32_be(uint8_t* buffer, uint32_t value) {
    buffer[0] = (uint8_t)(value >> 24U);
    buffer[1] = (uint8_t)(value >> 16U);
    buffer[2] = (uint8_t)(value >> 8U);
    buffer[3] = (uint8_t)value;
}

/** \brief           Write a big-endian value to a validated 8-byte span. */
static inline void xgl_serialize_u64_be(uint8_t* buffer, uint64_t value) {
    xgl_serialize_u32_be(buffer, (uint32_t)(value >> 32U));
    xgl_serialize_u32_be(buffer + 4U, (uint32_t)value);
}

#endif /* XGL_WIRE_BYTES_H */
