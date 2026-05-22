/*
    CRC16 utility for packet integrity checking
    Implementation file
    Made by: Neo Vorsatz
*/

#include "crc.h"

// Espressif includes
#include "esp_rom_crc.h"

// CRC16 ================================

uint16_t crc16(const uint8_t *data, size_t length) {
    // esp_rom_crc16_le lives in ROM — zero flash cost, no manual implementation needed.
    // Initial value 0xFFFF matches the CRC-16/IBM convention.
    return esp_rom_crc16_le(0xFFFF, data, length);
}

// ================================
