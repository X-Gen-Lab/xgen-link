/**
 * \file            xgl_serialize.c
 * \brief           Serialization utilities implementation
 * \author          Nexus Team
 */

#include <xgl/internal/xgl_serialize.h>
#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* Little-Endian Serialization (uint16_t)                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Serialize uint16_t to little-endian byte order
 * \details         Always uses byte-wise access for alignment safety
 */
void xgl_serialize_u16_le(uint8_t* buffer, uint16_t value) {
    if (buffer == NULL) {
        return;
    }
    
    /* Byte-wise serialization (alignment-safe) */
    buffer[0] = (uint8_t)(value & 0xFF);        /* LSB */
    buffer[1] = (uint8_t)((value >> 8) & 0xFF); /* MSB */
}

/**
 * \brief           Deserialize uint16_t from little-endian byte order
 * \details         Always uses byte-wise access for alignment safety
 */
uint16_t xgl_deserialize_u16_le(const uint8_t* buffer) {
    if (buffer == NULL) {
        return 0;
    }
    
    /* Byte-wise deserialization (alignment-safe) */
    uint16_t value = 0;
    value |= (uint16_t)buffer[0];           /* LSB */
    value |= (uint16_t)buffer[1] << 8;      /* MSB */
    
    return value;
}

/*---------------------------------------------------------------------------*/
/* Little-Endian Serialization (uint32_t)                                   */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Serialize uint32_t to little-endian byte order
 * \details         Always uses byte-wise access for alignment safety
 */
void xgl_serialize_u32_le(uint8_t* buffer, uint32_t value) {
    if (buffer == NULL) {
        return;
    }
    
    /* Byte-wise serialization (alignment-safe) */
    buffer[0] = (uint8_t)(value & 0xFF);         /* Byte 0 (LSB) */
    buffer[1] = (uint8_t)((value >> 8) & 0xFF);  /* Byte 1 */
    buffer[2] = (uint8_t)((value >> 16) & 0xFF); /* Byte 2 */
    buffer[3] = (uint8_t)((value >> 24) & 0xFF); /* Byte 3 (MSB) */
}

/**
 * \brief           Deserialize uint32_t from little-endian byte order
 * \details         Always uses byte-wise access for alignment safety
 */
uint32_t xgl_deserialize_u32_le(const uint8_t* buffer) {
    if (buffer == NULL) {
        return 0;
    }
    
    /* Byte-wise deserialization (alignment-safe) */
    uint32_t value = 0;
    value |= (uint32_t)buffer[0];           /* Byte 0 (LSB) */
    value |= (uint32_t)buffer[1] << 8;      /* Byte 1 */
    value |= (uint32_t)buffer[2] << 16;     /* Byte 2 */
    value |= (uint32_t)buffer[3] << 24;     /* Byte 3 (MSB) */
    
    return value;
}
