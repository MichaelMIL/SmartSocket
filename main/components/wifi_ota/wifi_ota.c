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
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_ota";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAXIMUM_RETRY 5

// Provisioning access point (started when STA connection fails)
#define WIFI_NVS_NAMESPACE   "wifi_cfg"
#define PROV_AP_SSID         "SmartSocket-Setup"
#define PROV_AP_CHANNEL      1
#define PROV_AP_MAX_CONN     2

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_wifi_connected = false;
static bool s_ap_mode = false;

/**
 * @brief Load WiFi credentials saved via the web UI from NVS
 *
 * @return true if a non-empty SSID was found
 */
static bool load_saved_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    size_t sl = ssid_len;
    bool found = (nvs_get_str(nvs, "ssid", ssid, &sl) == ESP_OK) && (ssid[0] != '\0');
    if (found) {
        size_t pl = password_len;
        if (nvs_get_str(nvs, "password", password, &pl) != ESP_OK) {
            password[0] = '\0';
        }
    }

    nvs_close(nvs);
    return found;
}

esp_err_t wifi_ota_save_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for WiFi credentials: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "password", (password != NULL) ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved (SSID: %s)", ssid);
    } else {
        ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
    }
    return err;
}

/**
 * @brief Switch to AP mode so WiFi can be configured via the web UI
 *
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t start_provisioning_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_config = {
        .ap = {
            .ssid = PROV_AP_SSID,
            .ssid_len = strlen(PROV_AP_SSID),
            .channel = PROV_AP_CHANNEL,
            .max_connection = PROV_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning AP: %s", esp_err_to_name(err));
        return err;
    }

    s_ap_mode = true;
    ESP_LOGW(TAG, "Provisioning AP started: connect to \"%s\" and open http://192.168.4.1 to configure WiFi", PROV_AP_SSID);
    return ESP_OK;
}

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

    // Credentials saved via the web UI (NVS) take precedence over compiled-in defaults
    static char saved_ssid[33];
    static char saved_password[65];
    const char *ssid = config->ssid;
    const char *password = config->password;
    if (load_saved_credentials(saved_ssid, sizeof(saved_ssid), saved_password, sizeof(saved_password))) {
        ESP_LOGI(TAG, "Using WiFi credentials saved in NVS (SSID: %s)", saved_ssid);
        ssid = saved_ssid;
        password = saved_password;
    }

    bool has_password = (password != NULL && password[0] != '\0');
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = has_password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
        },
    };

    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (has_password) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished. Connecting to SSID: %s", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID: %s", ssid);

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
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", ssid);

        // Fall back to a provisioning access point so WiFi can be configured
        // from the web UI at http://192.168.4.1 (WiFi Settings tab)
        if (start_provisioning_ap() == ESP_OK) {
            uint16_t port = (config->ota_port > 0) ? config->ota_port : 80;
            http_server_start(port);
        }
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
 * @brief Check whether the device is running the provisioning access point
 */
bool wifi_ota_is_ap_mode(void)
{
    return s_ap_mode;
}

/**
 * @brief Get current IP address
 */
esp_err_t wifi_ota_get_ip(char *ip_str, size_t len)
{
    if (ip_str == NULL || len < 16) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ap_mode) {
        // Report the provisioning AP address
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_ip_info_t ap_ip_info;
        if (ap_netif != NULL && esp_netif_get_ip_info(ap_netif, &ap_ip_info) == ESP_OK) {
            snprintf(ip_str, len, IPSTR, IP2STR(&ap_ip_info.ip));
            return ESP_OK;
        }
        return ESP_ERR_INVALID_STATE;
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

