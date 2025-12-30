/*
 * WiFi and OTA Update Component
 * 
 * Provides WiFi connectivity and OTA (Over-The-Air) update functionality.
 */

#include "wifi_ota.h"
#include "http_server.h"
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_ota";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAXIMUM_RETRY 5

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_wifi_connected = false;

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Connect to the AP failed");
        }
        s_wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_wifi_connected = true;
    }
}

/**
 * @brief Initialize WiFi and connect to network
 */
esp_err_t wifi_ota_init(const wifi_ota_config_t *config)
{
    if (config == NULL || config->ssid == NULL) {
        ESP_LOGE(TAG, "Invalid WiFi configuration");
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    strncpy((char*)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (config->password != NULL) {
        strncpy((char*)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished. Connecting to SSID: %s", config->ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s", config->ssid);
        
        // Start HTTP server for firmware uploads (optional)
        // You can disable this by not calling wifi_ota_start_http_server()
        // or by setting config->ota_url to NULL
        if (config->ota_url == NULL) {
            // Start web server on default port 80
            uint16_t port = (config->ota_port > 0) ? config->ota_port : 80;
            wifi_ota_start_http_server(port);
        }
        
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", config->ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Unexpected event");
        return ESP_ERR_INVALID_STATE;
    }
}

/**
 * @brief Start OTA update from URL
 */
esp_err_t wifi_ota_update(const char *url)
{
    if (url == NULL) {
        ESP_LOGE(TAG, "OTA URL is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_connected) {
        ESP_LOGE(TAG, "WiFi not connected");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting OTA update from: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed: %s", esp_err_to_name(err));
        return err;
    }

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        ESP_LOGE(TAG, "Complete data was not received");
        esp_https_ota_abort(https_ota_handle);
        return ESP_ERR_INVALID_SIZE;
    }

    err = esp_https_ota_finish(https_ota_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update successful, rebooting...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

/**
 * @brief Start OTA update from hostname and path
 */
esp_err_t wifi_ota_update_from_host(const char *hostname, const char *path, uint16_t port)
{
    if (hostname == NULL || path == NULL) {
        ESP_LOGE(TAG, "Invalid hostname or path");
        return ESP_ERR_INVALID_ARG;
    }

    char url[256];
    if (port == 443) {
        snprintf(url, sizeof(url), "https://%s%s", hostname, path);
    } else {
        snprintf(url, sizeof(url), "http://%s:%d%s", hostname, port, path);
    }

    return wifi_ota_update(url);
}

/**
 * @brief Get WiFi connection status
 */
bool wifi_ota_is_connected(void)
{
    return s_wifi_connected;
}

/**
 * @brief Get current IP address
 */
esp_err_t wifi_ota_get_ip(char *ip_str, size_t len)
{
    if (ip_str == NULL || len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_connected) {
        strncpy(ip_str, "Not connected", len - 1);
        ip_str[len - 1] = '\0';
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }

    snprintf(ip_str, len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

/**
 * @brief Start HTTP server for firmware uploads
 */
esp_err_t wifi_ota_start_http_server(uint16_t port)
{
    if (!s_wifi_connected) {
        ESP_LOGE(TAG, "WiFi not connected, cannot start HTTP server");
        return ESP_ERR_INVALID_STATE;
    }
    
    return http_server_start(port);
}

