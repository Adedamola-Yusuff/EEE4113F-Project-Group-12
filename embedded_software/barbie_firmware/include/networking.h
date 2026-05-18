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

//==================================================
// PROTOCOL CONFIGURATIONS 
//==================================================

/**
 * @brief Initializes the LoRa transceiver hardware and communication settings
 * 
 * Configures SPI communication, resets the LoRa module,
 * enables TCXO and RF switch control, and prepares the
 * module for LoRa transmission and reception.
 * 
 * @return `true` if initialization was successful,
 *         `false` if initialization failed
 */
bool lora_init(void);

/**
 * @brief Sends data over LoRa
 * 
 * Transmits a packet of data using the configured
 * LoRa radio settings.
 * 
 * @param data Pointer to the data bytes to send
 * @param length The number of bytes to send
 * 
 * @return `true` on successful transmission,
 *         `false` on transmission failure
 */
bool sendLoRa(uint8_t *data, uint8_t length);

/**
 * @brief Receives data over LoRa
 * 
 * Places the LoRa transceiver into receive mode and
 * waits for an incoming packet.
 * 
 * @param data Pointer to the buffer where received data
 *             will be stored
 * @param length Pointer to a variable where the received
 *               packet length will be stored
 * 
 * @return `true` if a valid packet was received,
 *         `false` if reception failed or timed out
 */
bool receiveLoRa(uint8_t *data, uint8_t *length);

/**
 * @brief Checks whether a received packet is a WiFi ON request
 * 
 * Verifies whether the received LoRa packet contains a valid
 * `WIFI_ON_REQUEST` command intended for Node B.
 * 
 * @param data Pointer to the received packet data
 * 
 * @return `true` if the packet is a valid WiFi ON request,
 *         `false` otherwise
 */
bool isWifiOnRequest(uint8_t *data);

bool isGpsUpdateRequest(uint8_t *data);
//==================================================
//TEST FUNCTIONS
//==================================================

// void lora_test_loop(void);
// bool lora_run_diagnostics(void);
// bool lora_send_test_packet(void);

#endif