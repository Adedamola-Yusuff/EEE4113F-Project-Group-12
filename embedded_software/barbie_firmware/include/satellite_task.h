/*
    Handling the task of transmitting data and GPS over satellite
    Header file
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef SATELLITE_TASK_H
#define SATELLITE_TASK_H

// Espressif includes
#include "freertos/task.h"

// TASK HANDLE ================================

extern TaskHandle_t satelliteTask;

// ================================
// PUBLIC API ================================

/**
 * @brief FreeRTOS task function for satellite data transmission.
 * Wakes on two events:
 *   - Satellite timer fires (normal cadence): transmits all unsent packets,
 *     with GPS appended as a tail to the final message.
 *   - LOW_BATTERY_MASK is set: sends a low-battery alert, then cancels
 *     both the satellite and share data timers permanently.
 * 
 * @param arg Unused
 */
void satelliteTaskFunc(void *arg);

// ================================

#endif
