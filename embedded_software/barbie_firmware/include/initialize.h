/*
    Handling the initialization of the ESP32
    Header file
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef INITIALIZE_H
#define INITIALIZE_H

// Standard include
#include <stdint.h>

// Espressif includes
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// EVENT GROUP ================================

// System event group handle — used by tasks and ISRs to signal events
extern EventGroupHandle_t systemEventGroup;

// Event bit masks
#define WIFI_DUMP_MASK      0b000001    // set when LoRa requests WiFi data-dump
#define LOW_BATTERY_MASK    0b000010    // set when "low battery" is signaled
#define SENSOR_DATA_MASK    0b000100    // set when new sensor data is available
#define SHARE_DATA_MASK     0b001000    // set when the share data timer fires
#define LORA_RECEIVE_MASK   0b010000    // set by NetSys when a peer wants to share

// ================================
// INITIALIZER ================================

/**
 * @brief Initialise the ESP32 (called before starting FreeRTOS scheduler), in this order:
 * - Turn off peripherals
 * - Turn off internal radios (Bluetooth and WiFi)
 * - Create the system event group
 * - Read device ID
 * - Initialise SD card storage
 * - Initialise low-battery interrupt
 * - Initialise sensor data-ready interrupt
 * - Initialise timers
 * - Create application tasks
 * - Configure light sleep
 * - Start timers
 * 
 * @return ESP_OK on success, or the first error encountered.
 */
esp_err_t init(void);

// ================================
// PUBLIC API ================================

/**
 * @brief Get the device ID.
 * 
 * @return Device ID (0-7)
 */
uint8_t getDeviceId(void);

/**
 * @brief Cancel both the satellite and share data timers permanently.
 * Called by the satellite task on a low-battery event. The timers are
 * never restarted after this call.
 */
void cancelTimers(void);

// ================================

#endif
