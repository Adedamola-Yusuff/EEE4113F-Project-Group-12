// Header guard
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// Includes
#include <stdbool.h>

//==================================================
// WiFi start and stop functions
//==================================================

/**
 * @brief Starts the WiFi Access Point on Node B
 * 
 * Configures and enables the ESP32-S3 WiFi Access Point (AP),
 * allowing external devices such as the ship computer to
 * connect directly to Node B over WiFi.
 * 
 * @return `true` if the WiFi AP started successfully,
 *         `false` if startup failed
 */
bool wifi_start_ap(void);

/**
 * @brief Stops the WiFi Access Point on Node B
 * 
 * Disables the ESP32-S3 WiFi Access Point and disconnects
 * any currently connected devices.
 * 
 * @return `true` if the WiFi AP was stopped successfully,
 *         `false` if shutdown failed
 */
bool wifi_stop_ap(void);

//==================================================
// Web server functions
//==================================================

/**
 * @brief Starts the HTTP web server on Node B
 * 
 * Initializes and starts the embedded web server used
 * for handling WiFi-based requests such as data transfer
 * and WiFi control commands.
 * 
 * Registers the required URI handlers including:
 * - `/` for basic connectivity testing
 * - `/data` for data transfer
 * - `/wifi_off` for WiFi shutdown requests
 * 
 * @return `true` if the web server started successfully,
 *         `false` if startup failed
 */
bool wifi_start_web_server(void);

#endif