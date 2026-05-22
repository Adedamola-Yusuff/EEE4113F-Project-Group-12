#include "wifi_manager.h"
#include "gps_manager.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_random.h"

#define WIFI_AP_SSID      "Buoy_Node_B"
#define WIFI_AP_PASSWORD  "12345678"
#define WIFI_AP_CHANNEL   6
#define WIFI_MAX_CONN     4

static bool wifi_initialized = false;

static bool wifi_active = false;

//---------------STATIC FUNCTIONS---------------

static esp_err_t root_handler(httpd_req_t *req)
{
    const char response[] = "Node B WiFi server is working!";
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t data_handler(httpd_req_t *req)
{
    printf("WiFi DATA_TRANSFER_START\n");

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=node_b_data.bin");

    const int total_size = 20 * 1024 * 1024; // 20 MB
    const int chunk_size = 1024;
    uint8_t buffer[1024];

    int sent = 0;

    while(sent < total_size)
    {
        for(int i = 0; i < chunk_size; i++)
        {
            buffer[i] = esp_random() & 0xFF;
        }

        esp_err_t ret = httpd_resp_send_chunk(req, (const char *)buffer, chunk_size);

        if(ret != ESP_OK)
        {
            printf("WiFi DATA_TRANSFER_FAILED\n");
            return ret;
        }

        sent += chunk_size;
    }

    httpd_resp_send_chunk(req, NULL, 0);

    printf("WiFi DATA_TRANSFER_COMPLETE\n");

    return ESP_OK;
}

static esp_err_t wifi_off_handler(httpd_req_t *req)
{
    printf("WiFi OFF request received.\n");

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, "WiFi switching off", HTTPD_RESP_USE_STRLEN);

    printf("WiFi switched off.\n");

    vTaskDelay(pdMS_TO_TICKS(500));
    wifi_stop_ap();

    return ESP_OK;
}

static esp_err_t location_handler(httpd_req_t *req)
{
    printf("WiFi LOCATION_REQUEST received.\n");

    gps_update_location();

    const char *location = gps_get_location_string();

    char response[128];

    snprintf(
        response,
        sizeof(response),
        "{ \"node\": \"B\", \"location\": \"%s\" }",
        location
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    printf("Location sent over WiFi: %s\n", location);

    return ESP_OK;
}

//==================================================
//WIFI START AND STOP FUNCTIONS
//==================================================

bool wifi_start_ap(void)
{   
    if(wifi_active)
    {
        printf("WiFi AP already active.\n");
        return true;
    }

    printf("Starting WiFi AP...\n");

    if(!wifi_initialized)
    {
        esp_err_t ret = nvs_flash_init();

        if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            nvs_flash_erase();
            nvs_flash_init();
        }

        esp_netif_init();
        esp_event_loop_create_default();

        esp_netif_create_default_wifi_ap();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);

        wifi_initialized = true;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    if(strlen(WIFI_AP_PASSWORD) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    wifi_active = true;

    printf("WiFi AP started.\n");
    printf("SSID: %s\n", WIFI_AP_SSID);
    printf("Password: %s\n", WIFI_AP_PASSWORD);

    return true;
}

bool wifi_stop_ap(void)
{
    printf("Stopping WiFi AP...\n");

    esp_wifi_stop();

    wifi_active = false;

    printf("WiFi AP stopped.\n");

    return true;
}

//==================================================
//WEB SERVER FUNCTIONS
//==================================================

bool wifi_start_web_server(void)
{
    printf("Starting web server...\n");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    esp_err_t ret = httpd_start(&server, &config);

    if(ret != ESP_OK)
    {
        printf("Web server failed to start.\n");
        return false;
    }

    httpd_uri_t root_uri = {\
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &root_uri);

    httpd_uri_t data_uri = {
        .uri = "/data",
        .method = HTTP_GET,
        .handler = data_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &data_uri);

    httpd_uri_t location_uri = {
        .uri = "/location",
        .method = HTTP_GET,
        .handler = location_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &location_uri);

    httpd_uri_t wifi_off_uri = {
        .uri = "/wifi_off",
        .method = HTTP_GET,
        .handler = wifi_off_handler,
        .user_ctx = NULL
    };
    
    httpd_register_uri_handler(server, &wifi_off_uri);

    printf("Web server started.\n");
    printf("Open: http://192.168.4.1/\n");
    printf("Open: http://192.168.4.1/data\n");

    return true;
}
