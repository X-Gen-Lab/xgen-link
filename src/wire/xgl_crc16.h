/**
 * \file            xgl_crc16.h
 * \brief           Private CRC-16/MODBUS used by the wire format
 * \author          X-Gen Lab
 */

#ifndef XGL_CRC16_H
#define XGL_CRC16_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** \brief           Calculate CRC-16/MODBUS with the standard initial state. */
uint16_t xgl_crc16_modbus(const uint8_t* data, size_t len);

/** \brief           Extend a CRC-16/MODBUS state with the next byte span. */
uint16_t xgl_crc16_modbus_update(uint16_t crc, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* XGL_CRC16_H */
