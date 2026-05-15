/**
 * @file init.c
 * @brief Initialization and power management for the Antarctic buoy firmware.
 *
 * Architecture overview
 * ─────────────────────
 * The system spends the majority of its life in light sleep, waking only on
 * scheduled timer events or hardware interrupts.  Deep sleep is deliberately
 * avoided as the primary sleep mode because:
 *   • Sensor ring-buffer and peer-state data must survive across sleep cycles.
 *   • Wake latency from light sleep (~1 ms) is far lower than deep sleep
 *     (~15 ms + re-init), which matters for satellite and LoRa timing windows.
 *
 * Peripherals are power-gated via GPIO-controlled load switches and/or their
 * own sleep/standby commands; they are only enabled inside the task that needs
 * them, then immediately disabled again.
 *
 * Timer / interrupt sources
 * ─────────────────────────
 *   ESP timer (software)  – periodic I2C sensor read
 *                         – periodic LoRa peer ping
 *                         – periodic GPS + satellite TX (normal cadence)
 *                         – fast GPS + satellite TX (recovery mode cadence)
 *   GPIO interrupt        – LOW_BAT_PIN  rising edge  → satellite low-bat TX
 *   Task notification     – satellite Rx task         → recovery mode flag
 *   Task notification     – LoRa Rx task              → WiFi dump mode flag
 */

#include "init.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

/*
#include "lora_task.h"
#include "power.h"
#include "satellite_task.h"
#include "sensor_task.h"
#include "wifi_dump_task.h"
*/

/* ── Logging tag ─────────────────────────────────────────────────────────── */
static const char *TAG = "init";

/* ── Timing constants (all in microseconds for esp_timer) ────────────────── */
#define SENSOR_READ_INTERVAL_US     (30ULL  * 1000 * 1000)   /*  30 s */
#define LORA_PING_INTERVAL_US       (120ULL * 1000 * 1000)   /*  2 min */
#define GPS_TX_NORMAL_INTERVAL_US   (600ULL * 1000 * 1000)   /* 10 min */
#define GPS_TX_RECOVERY_INTERVAL_US (60ULL  * 1000 * 1000)   /*  1 min */

/* ── GPIO pin assignments ────────────────────────────────────────────────── */
#define LOW_BAT_PIN          GPIO_NUM_4   /* Rising edge = low battery event  */
#define LORA_PWR_EN_PIN      GPIO_NUM_5   /* Active-high load switch          */
#define GPS_PWR_EN_PIN       GPIO_NUM_6
#define SAT_PWR_EN_PIN       GPIO_NUM_7
#define SENSOR_PWR_EN_PIN    GPIO_NUM_8   /* Can gate the I2C sensor VCC rail */

/* ── FreeRTOS handles (extern so tasks can signal each other) ────────────── */
TaskHandle_t  g_sensor_task_handle     = NULL;
TaskHandle_t  g_lora_task_handle       = NULL;
TaskHandle_t  g_satellite_task_handle  = NULL;
TaskHandle_t  g_wifi_dump_task_handle  = NULL;
EventGroupHandle_t g_system_event_group = NULL;

/* System-wide event bits */
#define EVT_RECOVERY_MODE   BIT0   /* Set by satellite Rx, cleared on exit  */
#define EVT_WIFI_DUMP       BIT1   /* Set by LoRa Rx, cleared after dump    */
#define EVT_LOW_BATTERY     BIT2   /* Set by GPIO ISR                       */

/* ── Private timer handles ───────────────────────────────────────────────── */
static esp_timer_handle_t s_sensor_timer;
static esp_timer_handle_t s_lora_ping_timer;
static esp_timer_handle_t s_gps_tx_timer;

/* ══════════════════════════════════════════════════════════════════════════
 *  ISR – Low battery GPIO
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * Called from the GPIO ISR service when LOW_BAT_PIN transitions low → high.
 * Sets EVT_LOW_BATTERY so the satellite task can transmit the alert on its
 * next wake without any polling overhead.
 *
 * IRAM_ATTR keeps this in fast RAM so it executes even if flash cache is
 * disabled during sleep transitions.
 */
static void IRAM_ATTR low_battery_isr_handler(void *arg)
{
    BaseType_t higher_prio_task_woken = pdFALSE;

    xEventGroupSetBitsFromISR(g_system_event_group,
                              EVT_LOW_BATTERY,
                              &higher_prio_task_woken);

    /* Wake the satellite task immediately rather than waiting for its timer */
    if (g_satellite_task_handle != NULL) {
        vTaskNotifyGiveFromISR(g_satellite_task_handle, &higher_prio_task_woken);
    }

    portYIELD_FROM_ISR(higher_prio_task_woken);
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Timer callbacks
 *  These run in the esp_timer task (high-priority), so they do the minimum
 *  work necessary: unblock the relevant FreeRTOS task via a notification.
 * ══════════════════════════════════════════════════════════════════════════ */

static void sensor_timer_cb(void *arg)
{
    if (g_sensor_task_handle) {
        xTaskNotifyGive(g_sensor_task_handle);
    }
}

static void lora_ping_timer_cb(void *arg)
{
    if (g_lora_task_handle) {
        xTaskNotifyGive(g_lora_task_handle);
    }
}

static void gps_tx_timer_cb(void *arg)
{
    if (g_satellite_task_handle) {
        xTaskNotifyGive(g_satellite_task_handle);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Static helpers
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * Configure all peripheral power-enable pins as outputs and drive them low,
 * ensuring every external module is off before we start anything else.
 */
static esp_err_t power_pins_init(void)
{
    const gpio_num_t pwr_pins[] = {
        LORA_PWR_EN_PIN,
        GPS_PWR_EN_PIN,
        SAT_PWR_EN_PIN,
        SENSOR_PWR_EN_PIN,
    };

    gpio_config_t cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = 0,
    };

    for (size_t i = 0; i < sizeof(pwr_pins) / sizeof(pwr_pins[0]); i++) {
        cfg.pin_bit_mask |= (1ULL << pwr_pins[i]);
    }

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    /* All rails off */
    for (size_t i = 0; i < sizeof(pwr_pins) / sizeof(pwr_pins[0]); i++) {
        gpio_set_level(pwr_pins[i], 0);
    }

    return ESP_OK;
}

/**
 * Configure the low-battery input pin with a falling-back ISR.
 * The power module holds the pin low at rest and raises it on a low-battery
 * event, so we trigger on the rising edge.
 */
static esp_err_t low_battery_interrupt_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << LOW_BAT_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   /* defined low at rest */
        .intr_type    = GPIO_INTR_POSEDGE,
    };

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE /* already installed */) {
        return ret;
    }

    return gpio_isr_handler_add(LOW_BAT_PIN, low_battery_isr_handler, NULL);
}

/**
 * Create the three periodic esp_timers.
 * Timers are created but NOT started here; start_timers() arms them once
 * tasks are ready to receive notifications.
 */
static esp_err_t create_timers(void)
{
    esp_timer_create_args_t args;
    esp_err_t ret;

    args = (esp_timer_create_args_t){
        .callback = sensor_timer_cb,
        .name     = "sensor_tmr",
    };
    ret = esp_timer_create(&args, &s_sensor_timer);
    if (ret != ESP_OK) return ret;

    args = (esp_timer_create_args_t){
        .callback = lora_ping_timer_cb,
        .name     = "lora_ping_tmr",
    };
    ret = esp_timer_create(&args, &s_lora_ping_timer);
    if (ret != ESP_OK) return ret;

    args = (esp_timer_create_args_t){
        .callback = gps_tx_timer_cb,
        .name     = "gps_tx_tmr",
    };
    ret = esp_timer_create(&args, &s_gps_tx_timer);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

/**
 * Arm all periodic timers.
 * Called after tasks are created so the first notification is never lost.
 */
static esp_err_t start_timers(void)
{
    esp_err_t ret;

    ret = esp_timer_start_periodic(s_sensor_timer, SENSOR_READ_INTERVAL_US);
    if (ret != ESP_OK) return ret;

    ret = esp_timer_start_periodic(s_lora_ping_timer, LORA_PING_INTERVAL_US);
    if (ret != ESP_OK) return ret;

    ret = esp_timer_start_periodic(s_gps_tx_timer, GPS_TX_NORMAL_INTERVAL_US);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

/**
 * Switch the GPS transmit timer between normal and recovery cadences.
 * Safe to call from any task; esp_timer_restart is thread-safe.
 *
 * @param recovery  true  → 1-minute interval (recovery mode)
 *                  false → 10-minute interval (normal mode)
 */
esp_err_t gps_tx_set_recovery_mode(bool recovery)
{
    uint64_t interval = recovery
                        ? GPS_TX_RECOVERY_INTERVAL_US
                        : GPS_TX_NORMAL_INTERVAL_US;

    ESP_LOGI(TAG, "GPS TX interval → %s", recovery ? "RECOVERY (1 min)" : "NORMAL (10 min)");
    return esp_timer_restart(s_gps_tx_timer, interval);
}

/**
 * Disable the internal Wi-Fi and Bluetooth radios.
 * The ESP32-S3 keeps these clocked even when unused unless explicitly shut
 * down.  We re-enable Wi-Fi only inside wifi_dump_task when instructed.
 */
static void disable_internal_radios(void)
{
    /* Disable BT – never used by this application */
    esp_bt_controller_disable();   /* no-op if already off */

    /* Wi-Fi is left in a disabled state; wifi_dump_task enables it on demand */
    esp_wifi_stop();               /* no-op if not started */
}

/**
 * Configure light sleep as the system idle behaviour.
 *
 * With auto-light-sleep enabled in sdkconfig (CONFIG_FREERTOS_USE_TICKLESS_IDLE)
 * the FreeRTOS idle hook automatically enters light sleep when all tasks are
 * blocked.  The GPIO and timer wakeup sources registered here are the only
 * signals that will exit sleep.
 *
 * We also register LOW_BAT_PIN as a light-sleep wakeup source so that a
 * battery event wakes the chip even if it is sleeping between timer ticks.
 */
static esp_err_t configure_light_sleep(void)
{
    /* Wake on LOW_BAT_PIN rising edge during light sleep */
    esp_err_t ret = gpio_wakeup_enable(LOW_BAT_PIN, GPIO_INTR_HIGH_LEVEL);
    if (ret != ESP_OK) return ret;

    ret = esp_sleep_enable_gpio_wakeup();
    if (ret != ESP_OK) return ret;

    /*
     * esp_timer wakeup is enabled automatically when tickless idle is on;
     * the scheduler will wake the chip before the next timer fires.
     * No explicit esp_sleep_enable_timer_wakeup() call is needed here.
     */

    ESP_LOGI(TAG, "Light sleep configured (tickless idle assumed in sdkconfig)");
    return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Top-level initialisation.  Call once from app_main() before starting
 *        the FreeRTOS scheduler (or immediately after — tasks block on their
 *        first ulTaskNotifyTake so no event is lost).
 *
 * Sequence
 * ────────
 *  1. Kill all peripheral power rails (safe, known state).
 *  2. Disable the Wi-Fi / BT radios (they are on by default after reset).
 *  3. Create the system event group used by ISR and inter-task signalling.
 *  4. Configure the low-battery GPIO interrupt.
 *  5. Create (but do not start) the three periodic timers.
 *  6. Create the four application tasks (all begin blocked).
 *  7. Configure light-sleep wakeup sources.
 *  8. Start the timers — from this point the system is live.
 *
 * @return ESP_OK on success, or the first ESP_ERR_* encountered.
 */
esp_err_t buoy_initialize(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "=== Buoy firmware initialising ===");

    /* 1. All peripheral rails off */
    ret = power_pins_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Peripheral power rails: OFF");

    /* 2. Internal radios off */
    disable_internal_radios();
    ESP_LOGI(TAG, "Wi-Fi / BT: disabled");

    /* 3. Event group */
    g_system_event_group = xEventGroupCreate();
    if (g_system_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    /* 4. Low-battery interrupt */
    ret = low_battery_interrupt_init();
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Low-battery interrupt: armed (GPIO %d, rising edge)", LOW_BAT_PIN);

    /* 5. Timers */
    ret = create_timers();
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Periodic timers: created");

    /* 6. Application tasks
     *    Stack sizes and priorities are conservative starting points; tune
     *    with uxTaskGetHighWaterMark() during integration testing.
     *
     *    All tasks start blocked (waiting for their first notification or
     *    event-group bit), so they add zero active CPU overhead until needed.
     */
    BaseType_t task_ret;
/* 
    task_ret = xTaskCreate(sensor_task, "sensor",
                           2048, NULL, 3, &g_sensor_task_handle);
    if (task_ret != pdPASS) { ESP_LOGE(TAG, "sensor_task create failed"); return ESP_ERR_NO_MEM; }

    task_ret = xTaskCreate(lora_task, "lora",
                           4096, NULL, 4, &g_lora_task_handle);
    if (task_ret != pdPASS) { ESP_LOGE(TAG, "lora_task create failed"); return ESP_ERR_NO_MEM; }

    task_ret = xTaskCreate(satellite_task, "satellite",
                           4096, NULL, 4, &g_satellite_task_handle);
    if (task_ret != pdPASS) { ESP_LOGE(TAG, "satellite_task create failed"); return ESP_ERR_NO_MEM; }

    task_ret = xTaskCreate(wifi_dump_task, "wifi_dump",
                           8192, NULL, 2, &g_wifi_dump_task_handle);
    if (task_ret != pdPASS) { ESP_LOGE(TAG, "wifi_dump_task create failed"); return ESP_ERR_NO_MEM; }

    ESP_LOGI(TAG, "Application tasks: created (all blocked)");
*/
    /* 7. Light sleep */
    ret = configure_light_sleep();
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
    if (ret != ESP_OK) return ret;

    /* 8. Start timers — system is now live */
    ret = start_timers();
    ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Timers started — system live");

    ESP_LOGI(TAG, "=== Initialisation complete ===");
    return ESP_OK;
}
