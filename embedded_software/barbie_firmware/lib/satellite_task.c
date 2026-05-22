/*
    Handling the task of transmitting data and GPS over satellite
    Implementation file
    Made by: Neo Vorsatz
*/

#include "satellite_task.h"

// Standard includes
#include <string.h>

// Espressif includes
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// Local includes
#include "hardware_config.h"
#include "initialize.h"
#include "networking.h"
#include "software_config.h"
#include "storage.h"

// Disable logging
#ifndef DEBUG
    #undef ESP_LOGI
    #define ESP_LOGI(tag, format, ...)  // defined as empty
#endif

// Logging tag
static const char *TAG = "satellite_task";

// CONFIGURATION ================================

/*
 * Maximum number of bytes the satellite modem can transmit in a single message.
 * Since packets are 256 bytes, this allows up to MAX_SATELLITE_MSG_LEN / 256
 * packets per message. Agree this value with NetSys.
 */
#define MAX_SATELLITE_MSG_LEN   512     // 2 packets per message; adjust to modem limit

/*
 * Maximum size of the GPS location string written by netGetGps().
 * Sized generously — agree exact format with NetSys.
 */
#define GPS_BUFFER_LEN          64

// ================================
// TASK HANDLE ================================

TaskHandle_t satelliteTask = NULL;

// ================================
// PRIVATE FUNCTIONS ================================

// TRANSMISSION ================================

/**
 * @brief Transmit all unsent packets over satellite, one message at a time.
 * Reads packets from the satellite index up to the head index, batching them
 * into messages of up to MAX_SATELLITE_MSG_LEN bytes. The satellite index is
 * advanced only after each confirmed transmission, so a failed message is
 * retried from the last confirmed point on the next timer tick.
 * 
 * @param gpsBuffer     GPS location string to append to the final message,
 *                      or NULL if GPS was unavailable.
 * @param gpsLength     Length of the GPS string in bytes, or 0 if unavailable.
 */
static void transmitUnsentPackets(const uint8_t *gpsBuffer, size_t gpsLength) {
    uint8_t message[MAX_SATELLITE_MSG_LEN];
    uint16_t messageLen = 0;
    uint8_t packetBuffer[256];

    uint32_t headIndex      = storageGetHeadIndex();
    uint32_t satelliteIndex = storageGetSatelliteIndex();

    // Nothing to send
    if (satelliteIndex >= headIndex) {
        ESP_LOGI(TAG, "No unsent packets — skipping transmission");
        return;
    }

    ESP_LOGI(TAG, "Transmitting packets %ld to %ld over satellite", satelliteIndex, headIndex-1);

    while (satelliteIndex < headIndex) {
        messageLen = 0;

        // Fill message buffer with as many whole packets as fit
        while (satelliteIndex < headIndex && (messageLen + 256) <= MAX_SATELLITE_MSG_LEN) {
            esp_err_t error = storageReadPacket(satelliteIndex, packetBuffer);
            if (error != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read packet %ld — aborting transmission", satelliteIndex);
                return;
            }
            memcpy(&message[messageLen], packetBuffer, 256);
            messageLen += 256;
            satelliteIndex++;
        }

        // On the final message, append GPS if available
        bool isFinalMessage = (satelliteIndex >= headIndex);
        if (isFinalMessage && gpsBuffer != NULL && gpsLength > 0) {
            memcpy(&message[messageLen], gpsBuffer, gpsLength);
            messageLen += gpsLength;
            ESP_LOGI(TAG, "GPS appended to final message");
        }

        // Transmit the message
        esp_err_t error = netSatelliteTransmit(message, messageLen);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "Satellite transmission failed — will retry from packet %ld next cycle",
                     storageGetSatelliteIndex());
            return;
        }

        // Advance satellite index for each successfully transmitted packet in this message
        uint32_t confirmedIndex = storageGetSatelliteIndex();
        while (confirmedIndex < satelliteIndex) {
            storageAdvanceSatelliteIndex();
            confirmedIndex++;
        }
        ESP_LOGI(TAG, "Satellite index advanced to %ld", storageGetSatelliteIndex());
    }
}

/**
 * @brief Perform a full satellite transmission session.
 * Attempts to get a GPS fix first; omits GPS tail if the fix fails.
 * Then transmits all unsent packets with the GPS appended to the final message.
 */
static void performTransmission(void) {
    // Attempt to get GPS location
    uint8_t gpsBuffer[GPS_BUFFER_LEN];
    size_t gpsLength = 0;

    #ifdef SEND_GPS
        esp_err_t gpsError = netGetGps(gpsBuffer, GPS_BUFFER_LEN);
        if (gpsError == ESP_OK) {
            gpsLength = strlen((char *)gpsBuffer);
            ESP_LOGI(TAG, "GPS fix obtained");
        } else {
            ESP_LOGI(TAG, "GPS fix failed — omitting from transmission");
        }
    #endif

    // Transmit unsent packets (with GPS tail if available)
    #ifdef TRANSMIT_DATA
        transmitUnsentPackets(gpsBuffer, gpsLength);
    #endif
}

// ================================
// LOW BATTERY ================================

/**
 * @brief Handle a low-battery event.
 * Sends a low-battery alert over satellite, then cancels both periodic timers
 * to disable all further transmissions and sharing.
 */
static void handleLowBattery(void) {
    ESP_LOGI(TAG, "Low battery event — sending alert");

    // Send low-battery alert
    esp_err_t error = netSatelliteSendLowBatteryAlert();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Low-battery alert transmission failed");
    }

    // Cancel both timers to conserve power — these are never restarted
    cancelTimers();
    ESP_LOGI(TAG, "Satellite and share data timers cancelled");
}

// ================================
// PUBLIC API ================================

void satelliteTaskFunc(void *arg) {
    while (1) {
        // Block until the satellite timer fires (via task notification)
        // or a low-battery event is signalled (via event group)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Check what woke us — low battery takes priority over a timer tick
        EventBits_t bits = xEventGroupGetBits(systemEventGroup);
        if (bits & LOW_BATTERY_MASK) {
            // Clear the bit so it doesn't re-trigger
            xEventGroupClearBits(systemEventGroup, LOW_BATTERY_MASK);
            handleLowBattery();

            // Block indefinitely — timers are cancelled, nothing will wake this task again
            ESP_LOGI(TAG, "Satellite task suspended (low battery)");
            vTaskSuspend(NULL);
        }

        // Normal timer wake — perform satellite transmission
        ESP_LOGI(TAG, "Satellite timer fired — beginning transmission session");
        performTransmission();
    }
}

// ================================
