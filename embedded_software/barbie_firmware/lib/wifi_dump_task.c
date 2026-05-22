/*
    Handling the task of dumping all data over WiFi
    Implementation file
    Made by: Neo Vorsatz
*/

#include "wifi_dump_task.h"

// Espressif includes
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// Local includes
#include "initialize.h"
#include "networking.h"
#include "storage.h"

// Disable logging
#ifndef DEBUG
    #undef ESP_LOGI
    #define ESP_LOGI(tag, format, ...)  // defined as empty
#endif

// Logging tag
static const char *TAG = "wifi_dump_task";

// TASK HANDLE ================================

TaskHandle_t wifiDumpTask = NULL;

// ================================
// PRIVATE FUNCTIONS ================================

/**
 * @brief Stream all packets from the SD card to the connected WiFi client.
 * Reads from index 0 up to the current head index, sending each packet
 * individually via netWifiSendPacket(). If any packet fails to send,
 * the dump is aborted — the client is expected to re-request if incomplete.
 * 
 * @return ESP_OK if all packets were sent, ESP_FAIL if any transmission failed,
 *         or an ESP_ERR_* code if a packet could not be read from the SD card.
 */
static esp_err_t dumpAllPackets(void) {
    uint32_t headIndex = storageGetHeadIndex();

    if (headIndex==0) {
        ESP_LOGI(TAG, "No packets to dump");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Dumping %ld packet(s) over WiFi", headIndex);

    uint8_t packetBuffer[256];
    for (uint32_t i=0; i<headIndex; i++) {
        // Read packet from SD card
        esp_err_t error = storageReadPacket(i, packetBuffer);
        if (error!=ESP_OK) {
            ESP_LOGE(TAG, "Failed to read packet %ld — aborting dump", i);
            return error;
        }

        // Send packet to connected client
        error = netWifiSendPacket(packetBuffer);
        if (error!=ESP_OK) {
            ESP_LOGE(TAG, "Failed to send packet %ld — aborting dump", i);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Sent packet %ld / %ld", i+1, headIndex);
    }

    ESP_LOGI(TAG, "Dump complete — %ld packet(s) sent", headIndex);
    return ESP_OK;
}

// ================================
// PUBLIC API ================================

void wifiDumpTaskFunc(void *arg) {
    while (1) {
        // Block until NetSys sets WIFI_DUMP_MASK via a LoRa command.
        // pdTRUE clears the bit on exit, re-arming for the next request.
        xEventGroupWaitBits(systemEventGroup, WIFI_DUMP_MASK, pdTRUE, pdFALSE, portMAX_DELAY);
        ESP_LOGI(TAG, "WiFi dump requested — starting");

        // Start WiFi access point
        esp_err_t error = netWifiStartAp();
        if (error!=ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WiFi AP — aborting dump");
            continue;
        }
        ESP_LOGI(TAG, "WiFi AP started");

        // Stream all packets to the connected client
        error = dumpAllPackets();
        if (error!=ESP_OK) {
            ESP_LOGE(TAG, "Dump incomplete — client should re-request");
        }

        // Tear down WiFi access point regardless of dump outcome
        error = netWifiStopAp();
        if (error!=ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop WiFi AP cleanly");
        } else {
            ESP_LOGI(TAG, "WiFi AP stopped — returning to sleep");
        }
    }
}

// ================================
