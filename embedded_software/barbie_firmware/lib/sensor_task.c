/*
    Handling the task of receiving and storing sensor data via I2C
    Implementation file
    Made by: Neo Vorsatz
*/

#include "sensor_task.h"

// Standard includes
#include <stdio.h>
#include <string.h>

// Espressif includes
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// Local includes
#include "crc.h"
#include "hardware_config.h"
#include "initialize.h"
#include "software_config.h"
#include "storage.h"

// Disable logging
#ifndef DEBUG
    #undef ESP_LOGI
    #define ESP_LOGI(tag, format, ...)  // defined as empty
#endif

// Logging tag
static const char *TAG = "sensor_task";

// I2C CONFIGURATION ================================

#define I2C_PORT            I2C_NUM_0
#define I2C_RX_BUFFER_SIZE  512         // comfortably larger than the largest single frame
#define I2C_TX_BUFFER_SIZE  0           // slave never initiates; no TX buffer needed

// ================================
// VIRTUAL REGISTER MAP ================================

/*
 * The sensor module always sends a fixed 3-part frame:
 *   [ REG : 1 byte ][ LEN : 1 byte ][ PAYLOAD : LEN bytes ]
 *
 * REG selects the operation. LEN is the number of payload bytes that follow.
 * This fixed framing sidesteps the ESP-IDF I2C slave driver's lack of clean
 * transaction boundaries.
 *
 * The sensor module must send the header within I2C_HEADER_TIMEOUT_MS of
 * asserting PIN_SENSOR_DATA_READY, and the payload within I2C_PAYLOAD_TIMEOUT_MS
 * of the header. If either window expires, the MCU returns to sleep.
 */
#define REG_DATA            0x01    // sensor is writing data bytes to be stored
#define REG_COMMAND         0x02    // sensor is issuing a command to the ESP32

#define MAX_PAYLOAD_LEN     252     // matches the packet payload field exactly

#define I2C_HEADER_TIMEOUT_MS   500 // window for receiving the 2-byte header
#define I2C_PAYLOAD_TIMEOUT_MS  500 // window for receiving the payload

// ================================
// PACKET STRUCTURE ================================

/*
 * Packet layout (256 bytes total):
 *   [ BARBIE ID : 1 byte ][ Sequence number : 1 byte ][ Payload : 252 bytes ][ CRC16 : 2 bytes ]
 *
 * BARBIE ID        — originating device's 3-bit hardware ID, stored in a full byte.
 * Sequence number  — increments with each flushed packet (wraps 255 → 0); used for
 *                    gap detection and reordering by the researchers.
 * Payload          — raw sensor bytes; internal structure defined by the sensor team.
 * CRC16            — computed over all 254 preceding bytes (ID + sequence + payload).
 */
#define PACKET_TOTAL_LEN    256
#define PACKET_HEADER_LEN   2       // BARBIE ID + sequence number
#define PACKET_PAYLOAD_LEN  252
#define PACKET_CRC_LEN      2
#define PACKET_CRC_OFFSET   (PACKET_HEADER_LEN + PACKET_PAYLOAD_LEN)   // byte 254

// ================================
// TASK HANDLE ================================

TaskHandle_t sensorTask = NULL;

// ================================
// PRIVATE STATE ================================

// Payload accumulator — persists across task sleep cycles until 252 bytes are collected.
// Declared static so it lives in BSS rather than on the task stack.
static uint8_t  payloadAccumulator[PACKET_PAYLOAD_LEN];
static uint16_t accumulatorCount = 0;

// Sequence number — wraps naturally from 255 → 0 via uint8_t overflow.
static uint8_t sequenceNumber = 0;

// ================================
// PRIVATE FUNCTIONS ================================

// I2C ================================

/**
 * @brief Initialise the ESP32 as an I2C slave.
 * Called once at task startup; peripheral ownership belongs to this task.
 * 
 * @return ESP_OK on success, or the first error encountered.
 */
static esp_err_t i2cSlaveInit(void) {
    // Slave configuration
    i2c_config_t config = {
        .mode                   = I2C_MODE_SLAVE,
        .sda_io_num             = PIN_I2C_SDA,
        .scl_io_num             = PIN_I2C_SCL,
        .sda_pullup_en          = GPIO_PULLUP_ENABLE,
        .scl_pullup_en          = GPIO_PULLUP_ENABLE,
        .slave.addr_10bit_en    = 0,
        .slave.slave_addr       = I2C_SLAVE_ADDRESS,
    };

    // Configure and install
    esp_err_t error = i2c_param_config(I2C_PORT, &config);
    if (error!=ESP_OK) {return error;}
    return i2c_driver_install(I2C_PORT, I2C_MODE_SLAVE, I2C_RX_BUFFER_SIZE, I2C_TX_BUFFER_SIZE, 0);
}

/**
 * @brief Read and dispatch one complete I2C frame from the slave RX buffer.
 * Waits up to I2C_HEADER_TIMEOUT_MS for the 2-byte header, then up to
 * I2C_PAYLOAD_TIMEOUT_MS for the payload.
 * 
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if a window expired,
 *         ESP_ERR_INVALID_SIZE if LEN exceeds MAX_PAYLOAD_LEN.
 */
static esp_err_t readAndDispatchFrame(void);

// ================================
// PACKET ================================

/**
 * @brief Flush the accumulator as a complete 256-byte packet to the SD card.
 * Assembles the header, copies the payload, computes the CRC16 over the first
 * 254 bytes, then passes the packet to storage. Resets the accumulator and
 * advances the sequence number regardless of write outcome — a dropped packet
 * is preferable to a corrupt or duplicate one.
 */
static void flushPacket(void) {
    uint8_t packet[PACKET_TOTAL_LEN];

    // Assemble header
    packet[0] = getDeviceId();
    packet[1] = sequenceNumber;

    // Copy payload
    memcpy(&packet[PACKET_HEADER_LEN], payloadAccumulator, PACKET_PAYLOAD_LEN);

    // Compute and append CRC16 over header + payload (first 254 bytes), little-endian
    uint16_t checksum = crc16(packet, PACKET_CRC_OFFSET);
    packet[PACKET_CRC_OFFSET]     = (uint8_t)(checksum & 0xFF);
    packet[PACKET_CRC_OFFSET + 1] = (uint8_t)((checksum >> 8) & 0xFF);

    // Write to SD card via storage module
    esp_err_t error = storageWritePacket(packet);
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "storageWritePacket failed — packet dropped (seq %d)", sequenceNumber);
    } else {
        ESP_LOGI(TAG, "Packet written (BARBIE ID %d, seq %d)", packet[0], sequenceNumber);
    }

    // Reset accumulator and advance sequence number
    accumulatorCount = 0;
    sequenceNumber++;
}

/**
 * @brief Accumulate incoming payload bytes into the SRAM buffer.
 * Calls flushPacket() whenever 252 bytes have been collected.
 * 
 * @param bytes     Pointer to incoming bytes
 * @param length    Number of bytes to accumulate
 */
static void accumulateData(const uint8_t *bytes, uint8_t length) {
    for (uint8_t i=0; i<length; i++) {
        payloadAccumulator[accumulatorCount++] = bytes[i];
        if (accumulatorCount==PACKET_PAYLOAD_LEN) {
            flushPacket();
        }
    }
}

// ================================
// COMMANDS ================================

/**
 * @brief Handle a REG_COMMAND frame from the sensor module.
 * Stub for future development — logs the command byte and returns.
 * 
 * @param payload   Pointer to the payload bytes
 * @param length    Number of payload bytes (typically 1 for a command byte)
 */
static void handleCommand(const uint8_t *payload, uint8_t length) {
    if (length==0) {
        ESP_LOGI(TAG, "Received empty command frame — ignoring");
        return;
    }

    // Log the command byte
    ESP_LOGI(TAG, "Received sensor command: 0x%02X (not yet implemented)", payload[0]);

    // TODO: dispatch on command value, e.g.:
    // switch (payload[0]) {
    //     case CMD_ENTER_SLEEP: ...
    //     case CMD_REQUEST_STATUS: ...
    // }
}

// ================================

static esp_err_t readAndDispatchFrame(void) {
    uint8_t header[2]; // [ REG, LEN ]

    // Read 2-byte header
    int received = i2c_slave_read_buffer(I2C_PORT, header, sizeof(header), pdMS_TO_TICKS(I2C_HEADER_TIMEOUT_MS));
    if (received < (int)sizeof(header)) {
        ESP_LOGE(TAG, "I2C header timeout or short read (%d bytes)", received);
        return ESP_ERR_TIMEOUT;
    }

    uint8_t reg = header[0];
    uint8_t len = header[1];

    // Validate payload length
    if (len > MAX_PAYLOAD_LEN) {
        ESP_LOGE(TAG, "Payload length %d exceeds maximum %d — discarding", len, MAX_PAYLOAD_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    // Read payload (may be zero-length for bare commands)
    uint8_t payload[MAX_PAYLOAD_LEN];
    if (len > 0) {
        received = i2c_slave_read_buffer(I2C_PORT, payload, len, pdMS_TO_TICKS(I2C_PAYLOAD_TIMEOUT_MS));
        if (received < (int)len) {
            ESP_LOGE(TAG, "I2C payload timeout (%d/%d bytes)", received, len);
            return ESP_ERR_TIMEOUT;
        }
    }

    // Dispatch on register address
    switch (reg) {
        case REG_DATA:
            ESP_LOGI(TAG, "REG_DATA: %d byte(s) received", len);
            accumulateData(payload, len);
            break;
        case REG_COMMAND:
            ESP_LOGI(TAG, "REG_COMMAND received");
            handleCommand(payload, len);
            break;
        default:
            ESP_LOGI(TAG, "Unknown register 0x%02X — ignoring", reg);
            break;
    }

    // Return
    return ESP_OK;
}

// ================================
// PUBLIC API ================================

void sensorTaskFunc(void *arg) {
    // Initialise I2C slave driver once at task startup
    esp_err_t error = i2cSlaveInit();
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "I2C slave init failed: %s — task exiting", esp_err_to_name(error));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "I2C slave initialised (address 0x%02X)", I2C_SLAVE_ADDRESS);

    while (1) {
        // Block until the sensor data-ready ISR sets SENSOR_DATA_MASK.
        // pdTRUE clears the bit on exit, re-arming for the next assertion.
        xEventGroupWaitBits(systemEventGroup, SENSOR_DATA_MASK, pdTRUE, pdFALSE, portMAX_DELAY);
        ESP_LOGI(TAG, "Data-ready event — reading I2C frame");

        // Read and dispatch the incoming frame
        error = readAndDispatchFrame();
        if (error!=ESP_OK) {
            ESP_LOGE(TAG, "Frame read failed: %s", esp_err_to_name(error));
            // Non-fatal: accumulator state preserved, waiting for next event
        }
    }
}

// ================================
