#include "gps_manager.h"
#include "hardware_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "driver/uart.h"

#define GPS_UART_NUM UART_NUM_1
#define GPS_BAUD_RATE 9600
#define GPS_BUFFER_SIZE 256

static char latest_gps_location[128] = "GPS not updated yet";

bool gps_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = GPS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    if(uart_driver_install(GPS_UART_NUM, GPS_BUFFER_SIZE * 2, 0, 0, NULL, 0) != ESP_OK)
    {
        printf("GPS UART driver install failed.\n");
        return false;
    }

    if(uart_param_config(GPS_UART_NUM, &uart_config) != ESP_OK)
    {
        printf("GPS UART config failed.\n");
        return false;
    }

    if(uart_set_pin(GPS_UART_NUM, GPS_RXD, GPS_TXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
    {
        printf("GPS UART pin config failed.\n");
        return false;
    }

    printf("GPS UART initialized.\n");
    return true;
}

bool gps_update_location(void)
{
    uint8_t data[GPS_BUFFER_SIZE];

    int length = uart_read_bytes(
        GPS_UART_NUM,
        data,
        GPS_BUFFER_SIZE - 1,
        pdMS_TO_TICKS(500)
    );

    if(length <= 0)
    {
        return false;
    }

    data[length] = '\0';
    printf("GPS raw chunk:\n%s\n", (char *)data);

    char *line = strtok((char *)data, "\n");

    while(line != NULL)
    {
        if(strncmp(line, "$GPGSV", 6) == 0)
        {
            printf("GPS GSV sentence:\n%s\n", line);
        }

        if(strchr(line, '*') == NULL)
        {
            line = strtok(NULL, "\n");
            continue;
        }

        if(strncmp(line, "$GPRMC", 6) != 0)
        {
            line = strtok(NULL, "\n");
            continue;
        }

        printf("GPS RMC sentence:\n%s\n", line);

        char line_copy[GPS_BUFFER_SIZE];
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';

        char *token;

        token = strtok(line_copy, ","); // $GPRMC
        token = strtok(NULL, ",");      // UTC time
        token = strtok(NULL, ",");      // Status

        if(token == NULL || token[0] != 'A')
        {
            line = strtok(NULL, "\n");
            continue;
        }

        char *lat_str = strtok(NULL, ",");
        char *lat_dir = strtok(NULL, ",");

        char *lon_str = strtok(NULL, ",");
        char *lon_dir = strtok(NULL, ",");

        if(lat_str == NULL || lat_dir == NULL ||
           lon_str == NULL || lon_dir == NULL ||
           strlen(lat_str) == 0 || strlen(lat_dir) == 0 ||
           strlen(lon_str) == 0 || strlen(lon_dir) == 0)
        {
            printf("Incomplete GPS sentence. Ignoring.\n");
            line = strtok(NULL, "\n");
            continue;
        }

        double lat_raw = atof(lat_str);

        int lat_deg = (int)(lat_raw / 100);
        double lat_min = lat_raw - (lat_deg * 100);

        double latitude = lat_deg + (lat_min / 60.0);

        if(lat_dir[0] == 'S')
        {
            latitude *= -1.0;
        }

        double lon_raw = atof(lon_str);

        int lon_deg = (int)(lon_raw / 100);
        double lon_min = lon_raw - (lon_deg * 100);

        double longitude = lon_deg + (lon_min / 60.0);

        if(lon_dir[0] == 'W')
        {
            longitude *= -1.0;
        }

        snprintf(
            latest_gps_location,
            sizeof(latest_gps_location),
            "%.1f,%.1f",
            latitude,
            longitude
        );

        printf("Parsed GPS location: %s\n", latest_gps_location);

        return true;
    }

    return false;
}

const char* gps_get_location_string(void)
{
    return latest_gps_location;
}

bool gps_wait_for_valid_location(uint32_t timeout_ms)
{
    uart_flush_input(GPS_UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(200));

    uint32_t elapsed = 0;

    while(elapsed < timeout_ms)
    {
        if(gps_update_location())
        {
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
        elapsed += 500;
    }

    return false;
}