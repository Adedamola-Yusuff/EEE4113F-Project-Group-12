/*
    Header file describing the software configurations
    Made by: Neo Vorsatz
*/

// Header guard
#ifndef SOFTWARE_CONFIG_H
#define SOFTWARE_CONFIG_H

// Includes
#include <stdbool.h>

// RECONFIGURATION FOR CLIENT ================================

// I2C Interface
#define I2C_SLAVE_ADDRESS 0x67 // I2C slave address

// Barbie-to-Barbie data sharing
#define DATA_SHARING // comment this out to stop device from sharing data via LoRa
// #define SHARE_PERIOD_OVERRIDE // uncomment this to override the data sharing period
#define SHARE_PERIOD (1ULL*60*60*1000*1000) // new data sharing period, if overriden (microseconds)

// Satellite data transmission
#define SEND_GPS // comment this out to stop device from sending its GPS
#define TRANSMIT_DATA // comment this out to stop device from sending its data over satellite
#define SATELLITE_PERIOD (24ULL*60*60*1000*1000) // 24 hours

// Device ID
// #define DEVICE_ID_OVERRIDE // uncomment this to override the device's ID
#define DEVICE_ID 0 // new device ID, if overriden (from 0 to 7)

// SD card data storage
#define DATA_FILENAME_PATH "/sdcard/data.bin" // filepath of the data file

// ================================
// DEBUGGING ================================

#define DEBUG // uncomment this to enable debug logging

// ================================
// FIXED CONSTANTS ================================

// Data sharing periods
#define SHARE_PERIOD_0 (120ULL*60*1000*1000) // 120 minutes
#define SHARE_PERIOD_1 (119ULL*60*1000*1000) // 119 minutes
#define SHARE_PERIOD_2 (118ULL*60*1000*1000) // 118 minutes
#define SHARE_PERIOD_3 (117ULL*60*1000*1000) // 117 minutes
#define SHARE_PERIOD_4 (116ULL*60*1000*1000) // 116 minutes
#define SHARE_PERIOD_5 (115ULL*60*1000*1000) // 115 minutes
#define SHARE_PERIOD_6 (114ULL*60*1000*1000) // 114 minutes
#define SHARE_PERIOD_7 (113ULL*60*1000*1000) // 113 minutes

// ================================

#endif