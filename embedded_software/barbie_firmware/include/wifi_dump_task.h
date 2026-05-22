/*
    Handling the task of dumping all data over WiFi
    Header file
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef WIFI_DUMP_TASK_H
#define WIFI_DUMP_TASK_H

// Espressif includes
#include "freertos/task.h"

// TASK HANDLE ================================

extern TaskHandle_t wifiDumpTask;

// ================================
// PUBLIC API ================================

/**
 * @brief FreeRTOS task function for dumping all SD card data over WiFi.
 * Blocks on WIFI_DUMP_MASK. When triggered by a LoRa command (via NetSys),
 * starts a WiFi access point, streams every packet from the SD card to the
 * connected client, then tears down the access point and returns to sleep.
 * 
 * All packets are sent regardless of satellite or share index state —
 * the researcher receives the complete dataset.
 * 
 * @param arg Unused
 */
void wifiDumpTaskFunc(void *arg);

// ================================

#endif
