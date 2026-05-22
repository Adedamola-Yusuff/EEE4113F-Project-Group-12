/*
    Handling the task of sharing data with peer BARBIEs over LoRa
    Implementation file
    Made by: Neo Vorsatz
*/

#include "share_data_task.h"

// Standard includes
#include <string.h>

// Espressif includes
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// Local includes
#include "crc.h"
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
static const char *TAG = "share_data_task";

// CONFIGURATION ================================

#define ACCEPTANCE_TIMEOUT_MS   1000    // max wait for "I'll accept" response (spec: 1 second)
#define RECEIVE_TIMEOUT_MS      5000    // max wait for incoming packet after sending acceptance
#define MAX_STRIKES             3       // consecutive failures before sharing is disabled

// ================================
// TASK HANDLE ================================

TaskHandle_t shareDataTask = NULL;

// ================================
// PRIVATE STATE ================================

// Consecutive failed sender attempts. Resets to 0 on a successful share.
// Sharing is permanently disabled once this reaches MAX_STRIKES.
static uint8_t strikeCount = 0;

// ================================
// PRIVATE FUNCTIONS ================================

// SENDER ================================

/**
 * @brief Find the next packet to share.
 * Scans forward from the current share index, skipping any packets that
 * originated from another BARBIE (device ID field doesn't match ours).
 * Foreign packets are immediately considered shared and their index is advanced.
 * 
 * @param index     Output — index of the next own packet to share
 * @return          true if an unsent own packet was found, false if none exist.
 */
static bool findNextOwnPacket(uint32_t *index) {
    uint8_t packetBuffer[256];
    uint8_t ownId = getDeviceId();

    while (storageGetShareIndex() < storageGetHeadIndex()) {
        uint32_t candidate = storageGetShareIndex();

        // Read the candidate packet
        esp_err_t error = storageReadPacket(candidate, packetBuffer);
        if (error!=ESP_OK) {
            ESP_LOGE(TAG, "Failed to read packet %ld — skipping", candidate);
            storageAdvanceShareIndex();
            continue;
        }

        // Check device ID (byte 0 of packet)
        if (packetBuffer[0] != ownId) {
            // Foreign packet — immediately considered shared, advance and continue
            ESP_LOGI(TAG, "Packet %ld is foreign (ID %d) — skipping", candidate, packetBuffer[0]);
            storageAdvanceShareIndex();
            continue;
        }

        // Found an own packet
        *index = candidate;
        return true;
    }

    // No own packets left to share
    return false;
}

/**
 * @brief Perform one sender attempt: broadcast, wait for acceptance, transmit.
 * Increments the strike counter on failure and resets it on success.
 * If MAX_STRIKES is reached, suspends the task permanently.
 */
static void performSenderAttempt(void) {
    uint32_t packetIndex;

    // Find the next own packet to share
    if (!findNextOwnPacket(&packetIndex)) {
        ESP_LOGI(TAG, "No own packets to share — skipping");
        return;
    }

    // Read the packet
    uint8_t packetBuffer[256];
    esp_err_t error = storageReadPacket(packetIndex, packetBuffer);
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "Failed to read packet %ld — aborting attempt", packetIndex);
        return;
    }

    // Broadcast "want to share"
    error = netLoraBroadcastWantToShare();
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast want-to-share");
        return;
    }

    // Wait for acceptance
    error = netLoraListenForAcceptance(ACCEPTANCE_TIMEOUT_MS);
    if (error!=ESP_OK) {
        // No acceptance received — add a strike
        strikeCount++;
        ESP_LOGI(TAG, "No acceptance received — strike %d/%d", strikeCount, MAX_STRIKES);

        if (strikeCount>=MAX_STRIKES) {
            ESP_LOGI(TAG, "Max strikes reached — suspending share data task permanently");
            vTaskSuspend(NULL);
        }
        return;
    }

    // Transmit the packet
    error = netLoraTransmitPacket(packetBuffer);
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "Packet transmission failed");
        return;
    }

    // Success — advance share index and reset strike counter
    storageAdvanceShareIndex();
    strikeCount = 0;
    ESP_LOGI(TAG, "Packet %ld shared successfully — strike counter reset", packetIndex);
}

// ================================
// RECIPIENT ================================

/**
 * @brief Verify the CRC16 of a received 256-byte packet.
 * Recomputes the CRC16 over the first 254 bytes and compares against
 * the last 2 bytes of the packet (little-endian).
 * 
 * @param packet    Pointer to a 256-byte packet
 * @return          true if the CRC matches, false if the packet is corrupted.
 */
static bool verifyPacketCrc(const uint8_t *packet) {
    uint16_t computed  = crc16(packet, 254);
    uint16_t received  = (uint16_t)(packet[254]) | ((uint16_t)(packet[255]) << 8);
    return computed==received;
}

/**
 * @brief Perform the recipient role: apply delay, send acceptance, receive and store packet.
 */
static void performRecipientRole(void) {
    // Apply 100 * ID ms delay to prevent simultaneous acceptances from multiple BARBIEs
    uint32_t delay = 100 * getDeviceId();
    if (delay > 0) {
        vTaskDelay(pdMS_TO_TICKS(delay));
    }

    // Send acceptance
    esp_err_t error = netLoraSendAcceptance();
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "Failed to send acceptance");
        return;
    }

    // Receive packet
    uint8_t packetBuffer[256];
    error = netLoraReceivePacket(packetBuffer, RECEIVE_TIMEOUT_MS);
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive packet — dropping");
        return;
    }

    // Verify CRC16
    if (!verifyPacketCrc(packetBuffer)) {
        ESP_LOGE(TAG, "CRC16 mismatch — dropping corrupted packet");
        return;
    }

    // Store packet on SD card
    error = storageWritePacket(packetBuffer);
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "Failed to store received packet");
        return;
    }

    ESP_LOGI(TAG, "Received and stored packet from BARBIE ID %d", packetBuffer[0]);
}

// ================================
// PUBLIC API ================================

void shareDataTaskFunc(void *arg) {
#ifdef DATA_SHARING
    while (1) {
        // Block until either the share data timer or a LoRa receive interrupt fires.
        // Wait for any bit — the dispatch below determines which role to perform.
        EventBits_t bits = xEventGroupWaitBits(
            systemEventGroup,
            SHARE_DATA_MASK | LORA_RECEIVE_MASK,
            pdTRUE,         // clear triggered bit on exit
            pdFALSE,        // wake on any bit
            portMAX_DELAY
        );

        if (bits & LORA_RECEIVE_MASK) {
            // Recipient role — another BARBIE wants to share with us
            ESP_LOGI(TAG, "LoRa receive interrupt — performing recipient role");
            performRecipientRole();
        } else if (bits & SHARE_DATA_MASK) {
            // Sender role — our timer fired, time to share
            ESP_LOGI(TAG, "Share data timer fired — performing sender role");
            performSenderAttempt();
        }
    }
#else
    // DATA_SHARING is disabled — task exits immediately and does nothing.
    // The task handle remains valid but the task consumes no resources.
    ESP_LOGI(TAG, "DATA_SHARING disabled — share data task exiting");
    vTaskDelete(NULL);
#endif
}

// ================================
