/*
    Handling all SD card storage operations
    Implementation file
    Made by: Neo Vorsatz
*/

#include "storage.h"

// Standard includes
#include <stdio.h>
#include <string.h>

// Espressif includes
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

// Local includes
#include "hardware_config.h"
#include "software_config.h"

// Disable logging
#ifndef DEBUG
    #undef ESP_LOGI
    #define ESP_LOGI(tag, format, ...)  // defined as empty
#endif

// Logging tag
static const char *TAG = "storage";

// CONFIGURATION ================================

#define SD_MOUNT_POINT      "/sdcard"
#define SD_DATA_FILEPATH    (SD_MOUNT_POINT "/" DATA_FILENAME)  // → "/sdcard/data.bin"
#define SD_SPI_CLOCK_HZ     4000000     // 4 MHz — conservative for reliable SPI-mode operation
#define PACKET_LEN          256

// ================================
// PRIVATE STATE ================================

/*
 * Three indices tracking the state of the data file.
 * All initialise to zero at boot and are never persisted — a reset is
 * not expected during normal deployment (documented design decision).
 *
 * headIndex        — total packets written; also the index of the next packet to be written.
 * shareIndex       — packets [0, shareIndex) have been shared via LoRa.
 * satelliteIndex   — packets [0, satelliteIndex) have been sent over satellite.
 */
static uint32_t headIndex      = 0;
static uint32_t shareIndex     = 0;
static uint32_t satelliteIndex = 0;

// ================================
// INITIALISATION ================================

/**
 * @brief Mount the SD card over SPI and initialise the FatFS filesystem.
 * Must be called once during initialisation, before any other storage function.
 * 
 * @return ESP_OK on success, or the first error encountered.
 */
esp_err_t storageInit(void) {
    // SPI bus configuration
    spi_bus_config_t busConfig = {
        .mosi_io_num     = PIN_SPI_MOSI,
        .miso_io_num     = PIN_SPI_MISO,
        .sclk_io_num     = PIN_SPI_SCK,
        .quadwp_io_num   = -1,  // not used
        .quadhd_io_num   = -1,  // not used
        .max_transfer_sz = 4096,
    };

    // Initialise the SPI bus (shared with LoRa; ESP-IDF handles concurrent devices)
    esp_err_t error = spi_bus_initialize(SPI2_HOST, &busConfig, SDSPI_DEFAULT_DMA);
    if (error!=ESP_OK && error!=ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means the bus is already initialised — acceptable
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(error));
        return error;
    }

    // SD card SPI device configuration
    sdspi_device_config_t deviceConfig = SDSPI_DEVICE_CONFIG_DEFAULT();
    deviceConfig.gpio_cs   = PIN_CS_SD_CARD;
    deviceConfig.host_id   = SPI2_HOST;

    // FatFS mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mountConfig = {
        .format_if_mount_failed = false,    // never format — data loss risk
        .max_files              = 4,
        .allocation_unit_size   = 512,
    };

    // SPI host configuration
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SD_SPI_CLOCK_HZ / 1000;

    // Mount the SD card
    sdmmc_card_t *card;
    error = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &deviceConfig, &mountConfig, &card);
    if (error!=ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(error));
        return error;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", SD_MOUNT_POINT);
    return ESP_OK;
}

// ================================
// WRITE ================================

/**
 * @brief Append a 256-byte packet to the data file and advance the head index.
 * 
 * @param packet    Pointer to exactly 256 bytes
 * @return          ESP_OK on success, ESP_FAIL if the write was incomplete.
 */
esp_err_t storageWritePacket(const uint8_t *packet) {
    // Open in append+binary mode — creates the file if it doesn't exist
    FILE *fp = fopen(SD_DATA_FILEPATH, "ab");
    if (fp==NULL) {
        ESP_LOGE(TAG, "Failed to open %s for writing", SD_DATA_FILEPATH);
        return ESP_FAIL;
    }

    // Write packet
    size_t written = fwrite(packet, 1, PACKET_LEN, fp);
    fclose(fp);

    if (written!=PACKET_LEN) {
        ESP_LOGE(TAG, "Incomplete write (%d/%d bytes) — packet dropped (index %ld)",
                 written, PACKET_LEN, headIndex);
        return ESP_FAIL;
    }

    // Advance head index
    headIndex++;
    ESP_LOGI(TAG, "Packet written (head index now %ld)", headIndex);
    return ESP_OK;
}

// ================================
// READ ================================

/**
 * @brief Read a 256-byte packet at the given index from the data file.
 * 
 * @param index     Packet index (0 = first packet written)
 * @param buffer    Pointer to a 256-byte buffer to read into
 * @return          ESP_OK on success, ESP_ERR_INVALID_ARG if index is out of range,
 *                  ESP_FAIL if the read was incomplete.
 */
esp_err_t storageReadPacket(uint32_t index, uint8_t *buffer) {
    // Validate index
    if (index>=headIndex) {
        ESP_LOGE(TAG, "Read index %ld out of range (head index %ld)", index, headIndex);
        return ESP_ERR_INVALID_ARG;
    }

    // Open in read+binary mode
    FILE *fp = fopen(SD_DATA_FILEPATH, "rb");
    if (fp==NULL) {
        ESP_LOGE(TAG, "Failed to open %s for reading", SD_DATA_FILEPATH);
        return ESP_FAIL;
    }

    // Seek to the packet's byte offset — each packet is exactly 256 bytes
    long offset = (long)index * PACKET_LEN;
    if (fseek(fp, offset, SEEK_SET)!=0) {
        ESP_LOGE(TAG, "fseek to offset %ld failed", offset);
        fclose(fp);
        return ESP_FAIL;
    }

    // Read packet
    size_t bytesRead = fread(buffer, 1, PACKET_LEN, fp);
    fclose(fp);

    if (bytesRead!=PACKET_LEN) {
        ESP_LOGE(TAG, "Incomplete read (%d/%d bytes) at index %ld", bytesRead, PACKET_LEN, index);
        return ESP_FAIL;
    }

    // Return
    return ESP_OK;
}

// ================================
// INDICES ================================

uint32_t storageGetHeadIndex(void) {
    return headIndex;
}

uint32_t storageGetShareIndex(void) {
    return shareIndex;
}

uint32_t storageGetSatelliteIndex(void) {
    return satelliteIndex;
}

void storageAdvanceShareIndex(void) {
    if (shareIndex<headIndex) {
        shareIndex++;
        ESP_LOGI(TAG, "Share index advanced to %ld", shareIndex);
    } else {
        ESP_LOGI(TAG, "storageAdvanceShareIndex called but share index already at head");
    }
}

void storageAdvanceSatelliteIndex(void) {
    if (satelliteIndex<headIndex) {
        satelliteIndex++;
        ESP_LOGI(TAG, "Satellite index advanced to %ld", satelliteIndex);
    } else {
        ESP_LOGI(TAG, "storageAdvanceSatelliteIndex called but satellite index already at head");
    }
}

// ================================
