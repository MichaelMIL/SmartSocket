/*
 * HTTP Server Component Header
 * 
 * Provides a simple HTTP server for firmware uploads via web interface.
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the HTTP server for firmware uploads
 * 
 * @param port Port number for the HTTP server (default: 80)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t http_server_start(uint16_t port);

/**
 * @brief Stop the HTTP server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t http_server_stop(void);

/**
 * @brief Check if HTTP server is running
 * 
 * @return true if server is running, false otherwise
 */
bool http_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // HTTP_SERVER_H

