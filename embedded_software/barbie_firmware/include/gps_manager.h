#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initializes the GPS UART interface on Node B
 * 
 * Configures the ESP32-S3 UART pins and baud rate used
 * to communicate with the GPS module.
 * 
 * @return `true` if GPS UART initialization was successful,
 *         `false` if initialization failed
 */
bool gps_init(void);

/**
 * @brief Updates the stored GPS location for Node B
 * 
 * Reads available GPS data from the UART interface and
 * updates the latest stored GPS location when a valid
 * GPS sentence is received.
 * 
 * @return `true` if new GPS data was received,
 *         `false` if no new GPS data was available
 */
bool gps_update_location(void);

/**
 * @brief Returns the latest stored GPS location string
 * 
 * Provides the most recently stored GPS location in a
 * string format suitable for logging or WiFi data transfer.
 * 
 * @return Pointer to the latest GPS location string
 */
const char* gps_get_location_string(void);

bool gps_wait_for_valid_location(uint32_t timeout_ms);

#endif