/**
 * @file init.h
 * @brief Public interface for buoy initialisation and power management.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdbool.h>

/* ── System event group bits ─────────────────────────────────────────────── */
#define EVT_RECOVERY_MODE   BIT0
#define EVT_WIFI_DUMP       BIT1
#define EVT_LOW_BATTERY     BIT2

/* ── Shared handles (defined in init.c, used by tasks) ───────────────────── */
extern TaskHandle_t       g_sensor_task_handle;
extern TaskHandle_t       g_lora_task_handle;
extern TaskHandle_t       g_satellite_task_handle;
extern TaskHandle_t       g_wifi_dump_task_handle;
extern EventGroupHandle_t g_system_event_group;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief Initialise all hardware, interrupts, timers, and tasks.
 *        Call once from app_main() before yielding to the scheduler.
 */
esp_err_t buoy_initialize(void);

/**
 * @brief Switch the GPS satellite-TX timer between normal and recovery mode.
 * @param recovery  true  = 1-minute cadence, false = 10-minute cadence
 */
esp_err_t gps_tx_set_recovery_mode(bool recovery);
