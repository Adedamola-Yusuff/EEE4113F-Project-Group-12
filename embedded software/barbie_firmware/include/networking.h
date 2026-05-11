/*
    Header file for interacting with networking peripherals
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef NETWORKING_H
#define NETWORKING_H

// Includes
#include <stdbool.h>
#include <stdint.h>

// PROTOCOL CONFIGURATIONS ================================

// ================================
// TYPE DEFINITIONS ================================

typedef struct {
    float latitude;
    float longitude;
} gpsLoc_t;

// ================================
// SENDING ================================

/**
 * @brief Sends data over LoRa
 * 
 * @param data The data bytes to send
 * @param length The number of bytes to send
 * 
 * @return `true` on success, `false` on failure
 */
bool sendLoRa(uint8_t *data, uint8_t length);

/**
 * @brief Sends data over satellite
 * 
 * @param data The data bytes to send
 * @param length The number of bytes to send
 * 
 * @return `true` on success, `false` on failure
 */
bool sendSatellite(uint8_t *data, uint8_t length);

/**
 * @brief Sends data over WiFi
 * 
 * @param data The data bytes to send
 * @param length The number of bytes to send
 * 
 * @return `true` on success, `false` on failure
 */
bool sendWiFi(uint8_t *data, uint8_t length);

// ================================
// RECEIVING ================================

/**
 * @brief Gets GPS location
 * 
 * @return `true` on success, `false` on failure
 */
bool getGPS(gpsLoc_t *location);

// ================================

#endif