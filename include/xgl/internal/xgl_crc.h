/**
 * \file            xgl_crc.h
 * \brief           CRC calculation functions
 * \author          X-Gen Lab
 */

#ifndef XGL_CRC_H
#define XGL_CRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/*---------------------------------------------------------------------------*/
/* CRC8 Functions                                                            */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Calculate CRC8 using MAXIM polynomial (0x31)
 * \param[in]       data: Data buffer
 * \param[in]       len: Data length in bytes
 * \return          CRC8 value
 */
uint8_t xgl_crc8_maxim(const uint8_t* data, size_t len);

/**
 * \brief           Calculate CRC8 incrementally
 * \param[in]       crc: Previous CRC value (use 0 for first call)
 * \param[in]       data: Data buffer
 * \param[in]       len: Data length in bytes
 * \return          Updated CRC8 value
 */
uint8_t xgl_crc8_maxim_update(uint8_t crc, const uint8_t* data, size_t len);

/*---------------------------------------------------------------------------*/
/* CRC16 Functions                                                           */
/*---------------------------------------------------------------------------*/

/**
 * \brief           Calculate CRC16 using MODBUS polynomial (0x8005)
 * \param[in]       data: Data buffer
 * \param[in]       len: Data length in bytes
 * \return          CRC16 value
 */
uint16_t xgl_crc16_modbus(const uint8_t* data, size_t len);

/**
 * \brief           Calculate CRC16 incrementally
 * \param[in]       crc: Previous CRC value (use 0xFFFF for first call)
 * \param[in]       data: Data buffer
 * \param[in]       len: Data length in bytes
 * \return          Updated CRC16 value
 */
uint16_t xgl_crc16_modbus_update(uint16_t crc, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* XGL_CRC_H */
