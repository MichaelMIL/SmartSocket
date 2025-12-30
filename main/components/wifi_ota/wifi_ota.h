/*
 * WiFi and OTA Update Component Header
 * 
 * Provides WiFi connectivity and OTA (Over-The-Air) update functionality.
 */

#ifndef WIFI_OTA_H
#define WIFI_OTA_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi configuration structure
 */
typedef struct {
    const char *ssid;           // WiFi SSID
    const char *password;       // WiFi password
    const char *ota_url;        // OTA update URL (optional, can be NULL)
    const char *ota_host;       // OTA server hostname (optional, can be NULL)
    uint16_t ota_port;          // OTA server port (default: 80)
} wifi_ota_config_t;

/**
 * @brief Initialize WiFi and connect to network
 * 
 * @param config WiFi and OTA configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ota_init(const wifi_ota_config_t *config);

/**
 * @brief Start OTA update from URL
 * 
 * @param url Full URL to the firmware binary (e.g., "http://example.com/firmware.bin")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ota_update(const char *url);

/**
 * @brief Start OTA update from hostname and path
 * 
 * @param hostname Server hostname
 * @param path Path to firmware binary (e.g., "/firmware.bin")
 * @param port Server port (default: 80)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ota_update_from_host(const char *hostname, const char *path, uint16_t port);

/**
 * @brief Get WiFi connection status
 * 
 * @return true if connected, false otherwise
 */
bool wifi_ota_is_connected(void);

/**
 * @brief Get current IP address
 * 
 * @param ip_str Output buffer for IP address string (at least 16 bytes)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ota_get_ip(char *ip_str, size_t len);

/**
 * @brief Start HTTP server for firmware uploads (optional)
 * 
 * @param port Port number for HTTP server (default: 80)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t wifi_ota_start_http_server(uint16_t port);

#ifdef __cplusplus
}
#endif

#endif // WIFI_OTA_H

