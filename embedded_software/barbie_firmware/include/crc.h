/*
    CRC16 utility for packet integrity checking
    Header file
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef CRC_H
#define CRC_H

// Includes
#include <stdint.h>
#include <stddef.h>

// CRC16 ================================

/**
 * @brief Compute CRC16 over a buffer of bytes, using the CRC-16/IBM convention.
 * Provides a 1-in-65536 (~0.0015%) undetected error rate over 256-byte packets.
 * Wraps esp_rom_crc16_le() — no manual implementation needed, zero flash cost.
 * 
 * @param data      Pointer to the data buffer
 * @param length    Number of bytes to process
 * @return          16-bit CRC value
 */
uint16_t crc16(const uint8_t *data, size_t length);

// ================================

#endif
