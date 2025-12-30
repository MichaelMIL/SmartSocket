/*
 * HTTP Server Component
 * 
 * Provides a simple HTTP server for firmware uploads via web interface.
 */

#include "http_server.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "http_server";

static httpd_handle_t server_handle = NULL;
static bool server_running = false;

// HTML page for firmware upload
static const char upload_html[] = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>SmartSocket Firmware Update</title>"
"<style>"
"body {"
"  font-family: Arial, sans-serif;"
"  max-width: 600px;"
"  margin: 50px auto;"
"  padding: 20px;"
"  background-color: #f5f5f5;"
"}"
"h1 {"
"  color: #333;"
"  text-align: center;"
"}"
".container {"
"  background: white;"
"  padding: 30px;"
"  border-radius: 10px;"
"  box-shadow: 0 2px 10px rgba(0,0,0,0.1);"
"}"
".form-group {"
"  margin-bottom: 20px;"
"}"
"label {"
"  display: block;"
"  margin-bottom: 8px;"
"  font-weight: bold;"
"  color: #555;"
"}"
"input[type='file'] {"
"  width: 100%;"
"  padding: 10px;"
"  border: 2px dashed #ddd;"
"  border-radius: 5px;"
"  background-color: #fafafa;"
"}"
"button {"
"  width: 100%;"
"  padding: 12px;"
"  background-color: #4CAF50;"
"  color: white;"
"  border: none;"
"  border-radius: 5px;"
"  font-size: 16px;"
"  font-weight: bold;"
"  cursor: pointer;"
"}"
"button:hover {"
"  background-color: #45a049;"
"}"
"button:disabled {"
"  background-color: #cccccc;"
"  cursor: not-allowed;"
"}"
".status {"
"  margin-top: 20px;"
"  padding: 15px;"
"  border-radius: 5px;"
"  display: none;"
"}"
".status.success {"
"  background-color: #d4edda;"
"  color: #155724;"
"  border: 1px solid #c3e6cb;"
"}"
".status.error {"
"  background-color: #f8d7da;"
"  color: #721c24;"
"  border: 1px solid #f5c6cb;"
"}"
".status.info {"
"  background-color: #d1ecf1;"
"  color: #0c5460;"
"  border: 1px solid #bee5eb;"
"}"
".progress {"
"  width: 100%;"
"  height: 20px;"
"  background-color: #f0f0f0;"
"  border-radius: 10px;"
"  overflow: hidden;"
"  margin-top: 10px;"
"  display: none;"
"}"
".progress-bar {"
"  height: 100%;"
"  background-color: #4CAF50;"
"  width: 0%;"
"  transition: width 0.3s;"
"}"
"</style>"
"</head>"
"<body>"
"<div class='container'>"
"<h1>🔌 SmartSocket Firmware Update</h1>"
"<form id='uploadForm' enctype='multipart/form-data'>"
"<div class='form-group'>"
"<label for='firmware'>Select Firmware File (.bin):</label>"
"<input type='file' id='firmware' name='firmware' accept='.bin' required>"
"</div>"
"<button type='submit' id='uploadBtn'>Upload & Update Firmware</button>"
"</form>"
"<div class='progress' id='progress'>"
"<div class='progress-bar' id='progressBar'></div>"
"</div>"
"<div class='status' id='status'></div>"
"</div>"
"<script>"
"document.getElementById('uploadForm').addEventListener('submit', async function(e) {"
"  e.preventDefault();"
"  const fileInput = document.getElementById('firmware');"
"  const file = fileInput.files[0];"
"  if (!file) {"
"    showStatus('Please select a firmware file', 'error');"
"    return;"
"  }"
"  const formData = new FormData();"
"  formData.append('firmware', file);"
"  const uploadBtn = document.getElementById('uploadBtn');"
"  const progress = document.getElementById('progress');"
"  const progressBar = document.getElementById('progressBar');"
"  uploadBtn.disabled = true;"
"  uploadBtn.textContent = 'Uploading...';"
"  progress.style.display = 'block';"
"  showStatus('Uploading firmware...', 'info');"
"  try {"
"    const xhr = new XMLHttpRequest();"
"    xhr.upload.addEventListener('progress', function(e) {"
"      if (e.lengthComputable) {"
"        const percentComplete = (e.loaded / e.total) * 100;"
"        progressBar.style.width = percentComplete + '%';"
"      }"
"    });"
"    xhr.addEventListener('load', function() {"
"      if (xhr.status === 200) {"
"        showStatus('Firmware uploaded successfully! Device will reboot...', 'success');"
"        setTimeout(function() {"
"          showStatus('Rebooting device...', 'info');"
"        }, 2000);"
"      } else {"
"        showStatus('Upload failed: ' + xhr.responseText, 'error');"
"        uploadBtn.disabled = false;"
"        uploadBtn.textContent = 'Upload & Update Firmware';"
"        progress.style.display = 'none';"
"      }"
"    });"
"    xhr.addEventListener('error', function() {"
"      showStatus('Upload error occurred', 'error');"
"      uploadBtn.disabled = false;"
"      uploadBtn.textContent = 'Upload & Update Firmware';"
"      progress.style.display = 'none';"
"    });"
"    xhr.open('POST', '/update');"
"    xhr.send(formData);"
"  } catch (error) {"
"    showStatus('Error: ' + error.message, 'error');"
"    uploadBtn.disabled = false;"
"    uploadBtn.textContent = 'Upload & Update Firmware';"
"    progress.style.display = 'none';"
"  }"
"});"
"function showStatus(message, type) {"
"  const status = document.getElementById('status');"
"  status.textContent = message;"
"  status.className = 'status ' + type;"
"  status.style.display = 'block';"
"}"
"</script>"
"</body>"
"</html>";

/**
 * @brief Handler for root path - serves the upload HTML page
 */
static esp_err_t upload_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, upload_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Handler for firmware upload
 */
static esp_err_t update_post_handler(httpd_req_t *req)
{
    // Get content length
    size_t content_len = req->content_len;
    ESP_LOGI(TAG, "Received firmware update request, size: %zu bytes", content_len);
    
    if (content_len > 0) {
        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "No OTA partition found");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "No OTA partition found", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        
        ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
                 update_partition->subtype, update_partition->address);
        
        esp_ota_handle_t ota_handle = 0;
        esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "OTA begin failed", HTTPD_RESP_USE_STRLEN);
            return err;
        }
        
        // Allocate buffer for receiving data
        const size_t buf_size = 4096;  // 4KB buffer
        char *buf = (char *)malloc(buf_size);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate buffer");
            esp_ota_abort(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Memory allocation failed", HTTPD_RESP_USE_STRLEN);
            return ESP_ERR_NO_MEM;
        }
        
        size_t received = 0;
        size_t content_length = req->content_len;
        bool is_multipart = false;
        
        // Check if this is multipart form data
        char content_type[64] = {0};
        if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK) {
            if (strstr(content_type, "multipart/form-data") != NULL) {
                is_multipart = true;
                ESP_LOGI(TAG, "Multipart form data detected");
            }
        }
        
        // Read and skip multipart header if present
        if (is_multipart) {
            // Read initial data to find the binary content start
            int initial_recv = httpd_req_recv(req, buf, buf_size);
            if (initial_recv < 0) {
                ESP_LOGE(TAG, "Failed to receive initial data");
                free(buf);
                esp_ota_abort(ota_handle);
                httpd_resp_set_status(req, "500 Internal Server Error");
                httpd_resp_send(req, "Receive failed", HTTPD_RESP_USE_STRLEN);
                return ESP_FAIL;
            }
            
            // Find the start of binary data (after multipart boundary and headers)
            char *binary_start = strstr(buf, "\r\n\r\n");
            if (binary_start != NULL) {
                binary_start += 4;  // Skip \r\n\r\n
                size_t header_size = binary_start - buf;
                size_t data_size = initial_recv - header_size;
                
                // Write the first chunk of binary data
                if (data_size > 0) {
                    err = esp_ota_write(ota_handle, (const void *)binary_start, data_size);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                        free(buf);
                        esp_ota_abort(ota_handle);
                        httpd_resp_set_status(req, "500 Internal Server Error");
                        httpd_resp_send(req, "OTA write failed", HTTPD_RESP_USE_STRLEN);
                        return err;
                    }
                    received += data_size;
                }
            } else {
                // No header found, treat as raw binary
                err = esp_ota_write(ota_handle, (const void *)buf, initial_recv);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                    free(buf);
                    esp_ota_abort(ota_handle);
                    httpd_resp_set_status(req, "500 Internal Server Error");
                    httpd_resp_send(req, "OTA write failed", HTTPD_RESP_USE_STRLEN);
                    return err;
                }
                received += initial_recv;
            }
        }
        
        // Continue receiving remaining data
        while (received < content_length) {
            int recv_len = httpd_req_recv(req, buf, buf_size);
            if (recv_len < 0) {
                if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                    continue;
                }
                ESP_LOGE(TAG, "Receive failed");
                free(buf);
                esp_ota_abort(ota_handle);
                httpd_resp_set_status(req, "500 Internal Server Error");
                httpd_resp_send(req, "Receive failed", HTTPD_RESP_USE_STRLEN);
                return ESP_FAIL;
            }
            
            if (recv_len == 0) {
                break;  // Connection closed
            }
            
            // For multipart, check if we've reached the end boundary
            if (is_multipart && recv_len > 0) {
                // Check if this chunk contains the end boundary
                char *boundary = strstr(buf, "\r\n--");
                if (boundary != NULL) {
                    // Found boundary, only write data before it
                    size_t data_size = boundary - buf;
                    if (data_size > 0) {
                        err = esp_ota_write(ota_handle, (const void *)buf, data_size);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                            free(buf);
                            esp_ota_abort(ota_handle);
                            httpd_resp_set_status(req, "500 Internal Server Error");
                            httpd_resp_send(req, "OTA write failed", HTTPD_RESP_USE_STRLEN);
                            return err;
                        }
                        received += data_size;
                    }
                    break;  // Reached end of multipart data
                }
            }
            
            err = esp_ota_write(ota_handle, (const void *)buf, recv_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                free(buf);
                esp_ota_abort(ota_handle);
                httpd_resp_set_status(req, "500 Internal Server Error");
                httpd_resp_send(req, "OTA write failed", HTTPD_RESP_USE_STRLEN);
                return err;
            }
            
            received += recv_len;
            ESP_LOGI(TAG, "Written %d bytes, total: %zu/%zu", recv_len, received, content_length);
        }
        
        free(buf);
        
        err = esp_ota_end(ota_handle);
        if (err != ESP_OK) {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Image validation failed", HTTPD_RESP_USE_STRLEN);
                return err;
            }
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "OTA end failed", HTTPD_RESP_USE_STRLEN);
            return err;
        }
        
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Set boot partition failed", HTTPD_RESP_USE_STRLEN);
            return err;
        }
        
        ESP_LOGI(TAG, "Firmware update successful, rebooting...");
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_send(req, "Firmware update successful! Device will reboot...", HTTPD_RESP_USE_STRLEN);
        
        // Give time for response to be sent
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_restart();
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "No content", HTTPD_RESP_USE_STRLEN);
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/**
 * @brief Start the HTTP server
 */
esp_err_t http_server_start(uint16_t port)
{
    if (server_running) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 10;
    config.max_open_sockets = 7;
    
    ESP_LOGI(TAG, "Starting HTTP server on port %d", port);
    
    if (httpd_start(&server_handle, &config) == ESP_OK) {
        // Register handlers
        httpd_uri_t upload_page = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = upload_page_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server_handle, &upload_page);
        
        httpd_uri_t update_uri = {
            .uri = "/update",
            .method = HTTP_POST,
            .handler = update_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server_handle, &update_uri);
        
        server_running = true;
        ESP_LOGI(TAG, "HTTP server started successfully on port %d", port);
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}

/**
 * @brief Stop the HTTP server
 */
esp_err_t http_server_stop(void)
{
    if (!server_running || server_handle == NULL) {
        return ESP_OK;
    }
    
    httpd_stop(server_handle);
    server_handle = NULL;
    server_running = false;
    ESP_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}

/**
 * @brief Check if HTTP server is running
 */
bool http_server_is_running(void)
{
    return server_running;
}

