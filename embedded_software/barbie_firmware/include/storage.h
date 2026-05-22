/*
    Handling all SD card storage operations
    Header file
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef STORAGE_H
#define STORAGE_H

// Includes
#include "esp_err.h"
#include <stdint.h>

// INITIALISATION ================================

/**
 * @brief Mount the SD card over SPI and initialise the FatFS filesystem.
 * Must be called once during initialisation, before any other storage function.
 * 
 * @return ESP_OK on success, or the first error encountered.
 */
esp_err_t storageInit(void);

// ================================
// WRITE ================================

/**
 * @brief Append a 256-byte packet to the data file and advance the head index.
 * 
 * @param packet    Pointer to exactly 256 bytes
 * @return          ESP_OK on success, ESP_FAIL if the write was incomplete.
 */
esp_err_t storageWritePacket(const uint8_t *packet);

// ================================
// READ ================================

/**
 * @brief Read a 256-byte packet at the given index from the data file.
 * 
 * @param index     Packet index (0 = first packet written)
 * @param buffer    Pointer to a 256-byte buffer to read into
 * @return          ESP_OK on success, ESP_ERR_INVALID_ARG if index is out of range,
 *                  ESP_FAIL if the read was incomplete.
 */
esp_err_t storageReadPacket(uint32_t index, uint8_t *buffer);

// ================================
// INDICES ================================

/**
 * @brief Return the total number of packets written since boot.
 */
uint32_t storageGetHeadIndex(void);

/**
 * @brief Return the index up to which packets have been shared via LoRa.
 */
uint32_t storageGetShareIndex(void);

/**
 * @brief Return the index up to which packets have been sent over satellite.
 */
uint32_t storageGetSatelliteIndex(void);

/**
 * @brief Advance the share index by one.
 * Called by the share data task after a packet has been successfully shared.
 */
void storageAdvanceShareIndex(void);

/**
 * @brief Advance the satellite index by one.
 * Called by the satellite task after a packet has been successfully transmitted.
 */
void storageAdvanceSatelliteIndex(void);

// ================================

#endif
