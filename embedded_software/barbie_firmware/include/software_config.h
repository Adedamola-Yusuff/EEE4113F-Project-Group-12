/*
    Header file describing the software configurations
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef SOFTWARE_CONFIG_H
#define SOFTWARE_CONFIG_H

// Includes
#include <stdbool.h>

// Data Sharing
const bool DATA_SHARING = true; // Set to `false` to stop device from sharing data via LoRa

// Device ID
const bool DEVICE_ID_OVERRIDE = false; // Set to `true` to override the device's ID
const int DEVICE_ID = 0; // New device ID, if overriden

#endif