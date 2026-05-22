/*
    Handling the task of sharing data with peer BARBIEs over LoRa
    Header file
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef SHARE_DATA_TASK_H
#define SHARE_DATA_TASK_H

// Espressif includes
#include "freertos/task.h"

// TASK HANDLE ================================

extern TaskHandle_t shareDataTask;

// ================================
// PUBLIC API ================================

/**
 * @brief FreeRTOS task function for peer-to-peer data sharing over LoRa.
 * Handles two roles determined by which event bit wakes the task:
 * 
 *   Sender   (SHARE_DATA_MASK, timer-driven):
 *     Finds the next unsent own packet, broadcasts a "want to share" signal,
 *     waits for an acceptance, then transmits the packet. Tracks consecutive
 *     failed attempts; after 3 strikes sharing is permanently disabled.
 * 
 *   Recipient (LORA_RECEIVE_MASK, NetSys interrupt-driven):
 *     Waits 100 * ID ms, sends an acceptance, receives a packet, verifies
 *     the CRC16, and stores it on the SD card.
 * 
 * Both roles are disabled at compile time if DATA_SHARING is not defined.
 * 
 * @param arg Unused
 */
void shareDataTaskFunc(void *arg);

// ================================

#endif
