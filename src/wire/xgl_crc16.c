/**
 * \file            xgl_crc16.c
 * \brief           CRC-16/MODBUS for wire header and frame checksums
 */

#include <wire/xgl_crc16.h>

/* Four rounds of the reflected 0xA001 polynomial per entry. A 32-byte
 * table gives bounded per-byte work without a 512-byte byte-index table. */
static const uint16_t modbus_nibble[16] = {
    0x0000U, 0xCC01U, 0xD801U, 0x1400U, 0xF001U, 0x3C00U, 0x2800U, 0xE401U,
    0xA001U, 0x6C00U, 0x7800U, 0xB401U, 0x5000U, 0x9C01U, 0x8801U, 0x4400U,
};

uint16_t xgl_crc16_modbus(const uint8_t* data, size_t len) {
    return xgl_crc16_modbus_update(UINT16_MAX, data, len);
}

uint16_t xgl_crc16_modbus_update(uint16_t crc, const uint8_t* data,
                                 size_t len) {
    if (data == NULL) {
        return crc;
    }

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        crc = (uint16_t)((crc >> 4U) ^ modbus_nibble[crc & 0x0FU]);
        crc = (uint16_t)((crc >> 4U) ^ modbus_nibble[crc & 0x0FU]);
    }

    return crc;
}
