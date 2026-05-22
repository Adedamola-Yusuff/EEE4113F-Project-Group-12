/*
    Handling the task of receiving and storing sensor data via I2C
    Header file
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

// Espressif includes
#include "freertos/task.h"

// TASK HANDLE ================================

extern TaskHandle_t sensorTask;

// ================================
// PUBLIC API ================================

/**
 * @brief FreeRTOS task function for receiving and storing sensor data via I2C.
 * Blocks on SENSOR_DATA_MASK, reads an I2C frame from the sensor module,
 * accumulates payload bytes in SRAM, and flushes 256-byte packets to the SD card.
 * 
 * Packet layout (256 bytes):
 *   [ BARBIE ID : 1 byte ][ Sequence number : 1 byte ][ Payload : 252 bytes ][ CRC16 : 2 bytes ]
 * 
 * @param arg Unused
 */
void sensorTaskFunc(void *arg);

// ================================

#endif
