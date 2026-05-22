/*
    Handling the initialization of the ESP32
    Implementation file
    Made by: Neo Vorsatz
*/

#include "initialize.h"

// Standard include
#include <stdint.h>

// Espressif includes
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// Local includes
#include "hardware_config.h"
#include "satellite_task.h"
#include "sensor_task.h"
#include "share_data_task.h"
#include "software_config.h"
#include "storage.h"
#include "wifi_dump_task.h"

// Disable logging
#ifndef DEBUG
    #undef ESP_LOGI
    #define ESP_LOGI(tag, format, ...)  // defined as empty
#endif

// Logging tag
static const char *TAG = "init";

// Device ID
static uint8_t deviceId = 0;

// Event group handle
EventGroupHandle_t systemEventGroup = NULL;

// Timer handles
static esp_timer_handle_t shareDataTimer;
static esp_timer_handle_t satelliteTimer;

// Forward declarations
static void shareDataTimerCallback(void *arg);
static void satelliteTimerCallback(void *arg);

// MISCELLANEOUS PRIVATE FUNCTIONS ================================

/**
 * @brief Read the 3-bit device ID from hardware pins or software override.
 * Pins use internal pull-ups; a jumper shunt pulls a pin to GND (reads 0).
 * Falls back to ID 0 on error.
 *
 * @return ESP_OK on success, or the first error encountered.
 */
static esp_err_t readDeviceId(void) {
    #ifdef DEVICE_ID_OVERRIDE
        // Read the device ID from the software configuration
        deviceId = DEVICE_ID & 0b111;
        ESP_LOGI(TAG, "Device ID: %d (software override)", deviceId);
    #else
        // Assume the ID is 0
        deviceId = 0;

        // Configure pins
        const gpio_num_t idPins[3] = {PIN_ID_0, PIN_ID_1, PIN_ID_2};
        gpio_config_t pinConfig = {
            .pin_bit_mask = (1ULL<<PIN_ID_0) | (1ULL<<PIN_ID_1) | (1ULL<<PIN_ID_2),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        esp_err_t error = gpio_config(&pinConfig);
        if (error!=ESP_OK) {
            ESP_LOGE(TAG, "Device ID pin config failed, defaulting to ID 0");
            deviceId = 0;
            return error;
        }

        // Read the pins to determine the ID
        for (int i=0; i<3; i++) {
            // Pin reads 1 when open (pull-up), 0 when shunted to GND.
            // A shunted pin (0) means that bit is set in the ID.
            if (gpio_get_level(idPins[i])==0) {
                deviceId |= (1<<i);
            }
        }

        // Log completion
        ESP_LOGI(TAG, "Device ID: %d (from pins)", deviceId);
    #endif

    // Return
    return ESP_OK;
}

/**
 * @brief Configure all peripheral power-enable pins as outputs and drive them low,
 * ensuring every external module is off before we start anything else.
 * 
 * @return ESP_OK on success, or the first error encountered.
 */
static esp_err_t powerPinsInit(void) {
    // Relevant power pins
    const gpio_num_t pwrPins[] = {
        PIN_LORA_NRESET
    };

    // Configuration for each of the power pins
    gpio_config_t pwrPinConfig = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    for (size_t i=0; i<sizeof(pwrPins)/sizeof(pwrPins[0]); i++) {
        pwrPinConfig.pin_bit_mask |= (1ULL<<pwrPins[i]);
    }

    // Configure the pins
    esp_err_t error = gpio_config(&pwrPinConfig);
    if (error!=ESP_OK) {return error;}

    // Disable the power to each module
    for (size_t i=0; i<sizeof(pwrPins)/sizeof(pwrPins[0]); i++) {
        gpio_set_level(pwrPins[i], 0);
    }

    // Return
    return ESP_OK;
}

/**
 * @brief Disable the internal WiFi and Bluetooth radios.
 * The ESP32-S3 keeps these clocked even when unused unless explicitly shut down.
 * WiFi will get re-enabled when data needs to be dumped.
 */
static void disableInternalRadios(void) {
    // esp_bt_controller_disable(); // disable Bluetooth — not needed; CONFIG_BT_ENABLED should be unset
    esp_wifi_stop(); // disable WiFi
}

/**
 * @brief Configure light sleep as the system idle behaviour;
 * the system enters light sleep when all tasks are blocked.
 * The system wakes up when low-battery is signalled, communications are received, or a timer triggers.
 */
static esp_err_t configureLightSleep(void) {
    // Wake up when low-battery is signalled
    esp_err_t error = gpio_wakeup_enable(PIN_LOW_BATTERY, GPIO_INTR_HIGH_LEVEL);
    if (error!=ESP_OK) {return error;}
    error = esp_sleep_enable_gpio_wakeup();
    if (error!=ESP_OK) {return error;}

    /*
     * esp_timer wakeup is enabled automatically when tickless idle is on;
     * the scheduler will wake the chip before the next timer fires.
     * No explicit esp_sleep_enable_timer_wakeup() call is needed here.
     */

    // Log completion
    ESP_LOGI(TAG, "Light sleep configured (tickless idle assumed in sdkconfig)");
    return ESP_OK;
}

// ================================
// LOW BATTERY ================================

/**
 * @brief Interrupt service routine for the low-battery event.
 * Wakes up the satellite task to send a low-battery alert.
 * 
 * @param arg Unused
 */
static void IRAM_ATTR lowBatteryIsrHandler(void *arg) {
    BaseType_t higherPrioTaskWoken = pdFALSE;

    // Set the low-battery bit in the event group
    xEventGroupSetBitsFromISR(systemEventGroup, LOW_BATTERY_MASK, &higherPrioTaskWoken);

    // Wake up the satellite task immediately via task notification
    if (satelliteTask!=NULL) {
        vTaskNotifyGiveFromISR(satelliteTask, &higherPrioTaskWoken);
    }
    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

/**
 * @brief Configure the low-battery input pin with a rising-edge ISR.
 * The power module holds the pin low at rest and raises it on a low-battery event.
 */
static esp_err_t lowBatteryInterruptInit(void) {
    // Pin configuration
    gpio_config_t pinConfig = {
        .pin_bit_mask = (1ULL<<PIN_LOW_BATTERY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // assume battery is fine
        .intr_type = GPIO_INTR_POSEDGE
    };
    esp_err_t error = gpio_config(&pinConfig);
    if (error!=ESP_OK) {return error;}

    // Install interrupt service
    error = gpio_install_isr_service(0);
    if (error!=ESP_OK && error!=ESP_ERR_INVALID_STATE) {return error;} // ESP_ERR_INVALID_STATE occurs when already installed

    // Add interrupt handler
    return gpio_isr_handler_add(PIN_LOW_BATTERY, lowBatteryIsrHandler, NULL);
}

// ================================
// SENSOR DATA READY ================================

/**
 * @brief Interrupt service routine for when the sensor is ready to give data.
 * 
 * @param arg Unused
 */
static void IRAM_ATTR sensorDataIsrHandler(void *arg) {
    BaseType_t higherPrioTaskWoken = pdFALSE;

    // Set the sensor data ready bit — sensor task wakes on this event bit directly
    xEventGroupSetBitsFromISR(systemEventGroup, SENSOR_DATA_MASK, &higherPrioTaskWoken);

    portYIELD_FROM_ISR(higherPrioTaskWoken);
}

/**
 * @brief Configure the sensor data-ready input pin with a rising-edge ISR.
 * The sensor module asynchronously decides when it wants to give data,
 * and requests to wake the ESP32 by pulling this pin high.
 */
static esp_err_t sensorInterruptInit(void) {
    // Pin configuration
    gpio_config_t pinConfig = {
        .pin_bit_mask = (1ULL<<PIN_SENSOR_DATA_READY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    esp_err_t error = gpio_config(&pinConfig);
    if (error!=ESP_OK) {return error;}

    // Install interrupt service
    error = gpio_install_isr_service(0);
    if (error!=ESP_OK && error!=ESP_ERR_INVALID_STATE) {return error;} // ESP_ERR_INVALID_STATE occurs when already installed

    // Add interrupt handler
    return gpio_isr_handler_add(PIN_SENSOR_DATA_READY, sensorDataIsrHandler, NULL);
}

// ================================
// TIMERS ================================

/**
 * @brief Callback for the share data timer.
 * Sets SHARE_DATA_MASK to unblock the share data task.
 * 
 * @param arg Unused
 */
static void shareDataTimerCallback(void *arg) {
    if (shareDataTask) {
        xEventGroupSetBits(systemEventGroup, SHARE_DATA_MASK);
    }
}

/**
 * @brief Callback for the satellite timer.
 * Notifies the satellite task via task notification.
 * 
 * @param arg Unused
 */
static void satelliteTimerCallback(void *arg) {
    if (satelliteTask) {
        xTaskNotifyGive(satelliteTask);
    }
}

/**
 * @brief Create the timers for sharing data and sending information over satellite.
 * 
 * @return ESP_OK on success, or the first error encountered.
 */
static esp_err_t createTimers(void) {
    esp_timer_create_args_t timerArgs;
    esp_err_t error;

    // Create data sharing timer
    timerArgs = (esp_timer_create_args_t){
        .callback = shareDataTimerCallback,
        .name = "share_data_timer",
    };
    error = esp_timer_create(&timerArgs, &shareDataTimer);
    if (error!=ESP_OK) {return error;}

    // Create satellite transmission timer
    timerArgs = (esp_timer_create_args_t){
        .callback = satelliteTimerCallback,
        .name = "satellite_timer",
    };
    error = esp_timer_create(&timerArgs, &satelliteTimer);
    if (error!=ESP_OK) {return error;}

    // Return
    return ESP_OK;
}

/**
 * @brief Activate all periodic timers.
 * 
 * @return ESP_OK on success, or the first error encountered.
 */
static esp_err_t startTimers(void) {
    esp_err_t error;

    // Start data sharing timer
    #ifdef SHARE_PERIOD_OVERRIDE
        error = esp_timer_start_periodic(shareDataTimer, SHARE_PERIOD);
    #else
        const uint64_t sharePeriods[8] = {
            SHARE_PERIOD_0, SHARE_PERIOD_1, SHARE_PERIOD_2, SHARE_PERIOD_3,
            SHARE_PERIOD_4, SHARE_PERIOD_5, SHARE_PERIOD_6, SHARE_PERIOD_7
        };
        error = esp_timer_start_periodic(shareDataTimer, sharePeriods[deviceId]);
    #endif
    if (error!=ESP_OK) {return error;}

    // Start satellite timer
    error = esp_timer_start_periodic(satelliteTimer, SATELLITE_PERIOD_NORMAL);
    if (error!=ESP_OK) {return error;}

    // Return
    return ESP_OK;
}

// ================================
// PUBLIC API ================================

esp_err_t init(void) {
    esp_err_t error;
    ESP_LOGI(TAG, "=== Buoy firmware initialising ===");

    // Turn off peripherals
    error = powerPinsInit();
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    if (error!=ESP_OK) {return error;}
    ESP_LOGI(TAG, "Peripherals: OFF");

    // Turn off internal radios (Bluetooth and WiFi)
    disableInternalRadios();
    ESP_LOGI(TAG, "Bluetooth & WiFi: disabled");

    // Create the system event group
    systemEventGroup = xEventGroupCreate();
    if (systemEventGroup==NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Read device ID
    error = readDeviceId();
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    // Non-fatal: continues with ID 0 on failure

    // Initialise SD card storage
    error = storageInit();
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    if (error!=ESP_OK) {return error;}
    ESP_LOGI(TAG, "SD card: mounted");

    // Initialise low-battery interrupt
    error = lowBatteryInterruptInit();
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    if (error!=ESP_OK) {return error;}
    ESP_LOGI(TAG, "Low-battery interrupt: armed (GPIO %d, rising edge)", PIN_LOW_BATTERY);

    // Initialise sensor data-ready interrupt
    error = sensorInterruptInit();
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    if (error!=ESP_OK) {return error;}
    ESP_LOGI(TAG, "Sensor interrupt: armed (GPIO %d, rising edge)", PIN_SENSOR_DATA_READY);

    // Initialise timers
    error = createTimers();
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    if (error!=ESP_OK) {return error;}
    ESP_LOGI(TAG, "Periodic timers: created");

    // Create application tasks — all start blocked until their first event
    BaseType_t createdTask;

    // Create task for receiving and storing sensor data
    createdTask = xTaskCreate(sensorTaskFunc, "sensor_task", 4096, NULL, 3, &sensorTask);
    if (createdTask!=pdPASS) {
        ESP_LOGE(TAG, "sensor_task create failed");
        return ESP_ERR_NO_MEM;
    }

    // Create task for sharing data with peers over LoRa
    createdTask = xTaskCreate(shareDataTaskFunc, "share_data_task", 4096, NULL, 4, &shareDataTask);
    if (createdTask!=pdPASS) {
        ESP_LOGE(TAG, "share_data_task create failed");
        return ESP_ERR_NO_MEM;
    }

    // Create task for satellite transmission
    createdTask = xTaskCreate(satelliteTaskFunc, "satellite_task", 4096, NULL, 4, &satelliteTask);
    if (createdTask!=pdPASS) {
        ESP_LOGE(TAG, "satellite_task create failed");
        return ESP_ERR_NO_MEM;
    }

    // Create task for dumping data over WiFi
    createdTask = xTaskCreate(wifiDumpTaskFunc, "wifi_dump_task", 8192, NULL, 2, &wifiDumpTask);
    if (createdTask!=pdPASS) {
        ESP_LOGE(TAG, "wifi_dump_task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Application tasks: created (all blocked)");

    // Configure light sleep
    error = configureLightSleep();
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    if (error!=ESP_OK) {return error;}

    // Start timers — system is now live
    error = startTimers();
    ESP_ERROR_CHECK_WITHOUT_ABORT(error);
    if (error!=ESP_OK) {return error;}
    ESP_LOGI(TAG, "Timers started — system live");

    // Log completion
    ESP_LOGI(TAG, "=== Initialisation complete ===");
    return ESP_OK;
}

uint8_t getDeviceId(void) {
    return deviceId;
}

void cancelTimers(void) {
    esp_timer_stop(shareDataTimer);
    esp_timer_stop(satelliteTimer);
    ESP_LOGI(TAG, "Satellite and share data timers cancelled");
}

// ================================
