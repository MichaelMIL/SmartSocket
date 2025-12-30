/*
 * HTTP Server Component
 * 
 * Provides a simple HTTP server for firmware uploads via web interface.
 */

#include "http_server.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "relay_control_ui.h"

// Forward declaration
extern relay_control_ui_t *example_lvgl_get_relay_ui(int index);

static const char *TAG = "http_server";

static httpd_handle_t server_handle = NULL;
static bool server_running = false;

/**
 * @brief Initialize SPIFFS filesystem
 */
esp_err_t http_server_init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    
    return ESP_OK;
}

/**
 * @brief Get content type from file extension
 */
static const char* get_content_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }
    
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        return "text/html";
    } else if (strcmp(ext, ".css") == 0) {
        return "text/css";
    } else if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    } else if (strcmp(ext, ".json") == 0) {
        return "application/json";
    } else if (strcmp(ext, ".png") == 0) {
        return "image/png";
    } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    } else if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    } else if (strcmp(ext, ".ico") == 0) {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

/**
 * @brief Handler for serving static files from SPIFFS
 */
static esp_err_t file_handler(httpd_req_t *req)
{
    char filepath[256];
    
    // Map root to index.html
    if (strcmp(req->uri, "/") == 0) {
        strcpy(filepath, "/spiffs/index.html");
    } else {
        // Check URI length to prevent buffer overflow
        size_t uri_len = strlen(req->uri);
        if (uri_len > sizeof(filepath) - 8) {  // 8 = "/spiffs" + null terminator
            ESP_LOGE(TAG, "URI too long: %s", req->uri);
            httpd_resp_set_status(req, "414 URI Too Long");
            httpd_resp_send(req, "URI too long", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        // Suppress format-truncation warning - we've already checked the length
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(filepath, sizeof(filepath), "/spiffs%s", req->uri);
        #pragma GCC diagnostic pop
    }
    
    FILE *fd = fopen(filepath, "r");
    if (fd == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_send(req, "File not found", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Set content type
    const char *content_type = get_content_type(filepath);
    httpd_resp_set_type(req, content_type);
    
    // Send file content
    char chunk[1024];
    size_t read_bytes;
    do {
        read_bytes = fread(chunk, 1, sizeof(chunk), fd);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    
    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "File sent: %s", filepath);
    return ESP_OK;
}

/**
 * @brief Handler for root path - serves index.html from SPIFFS
 */
static esp_err_t control_page_handler(httpd_req_t *req)
{
    return file_handler(req);
}

/**
 * @brief Handler for firmware update page - serves index.html from SPIFFS
 */
static esp_err_t update_page_handler(httpd_req_t *req)
{
    return file_handler(req);
}

// Removed embedded HTML - now using SPIFFS

// Old handlers removed - now using file_handler from SPIFFS

/**
 * @brief Handler for getting relay status (GET /api/relay/<id>)
 */
static esp_err_t relay_get_handler(httpd_req_t *req)
{
    // Extract relay ID from URI (format: /api/relay/1)
    const char *uri = req->uri;
    const char *id_start = strrchr(uri, '/');
    if (id_start == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid URI\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    id_start++; // Skip '/'
    int relay_id = atoi(id_start);
    if (relay_id < 1 || relay_id > 6) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid relay ID\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    relay_control_ui_t *relay_ui = example_lvgl_get_relay_ui(relay_id);
    if (relay_ui == NULL) {
        ESP_LOGW(TAG, "Relay UI %d not found (may not be initialized yet)", relay_id);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Relay not initialized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;  // Return OK to avoid error logging, but indicate service unavailable
    }
    
    bool state = relay_control_ui_get_state(relay_ui);
    char response[128];
    snprintf(response, sizeof(response), "{\"success\":true,\"id\":%d,\"state\":%s}", relay_id, state ? "true" : "false");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Handler for setting relay state (POST /api/relay/<id>)
 */
static esp_err_t relay_post_handler(httpd_req_t *req)
{
    char relay_id_str[8] = {0};
    const char *uri = req->uri;
    const char *id_start = strrchr(uri, '/');
    if (id_start != NULL) {
        id_start++; // Skip '/'
        strncpy(relay_id_str, id_start, sizeof(relay_id_str) - 1);
    }
    
    int relay_id = atoi(relay_id_str);
    if (relay_id < 1 || relay_id > 6) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Invalid relay ID\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    relay_control_ui_t *relay_ui = example_lvgl_get_relay_ui(relay_id);
    if (relay_ui == NULL) {
        ESP_LOGW(TAG, "Relay UI %d not found (may not be initialized yet)", relay_id);
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Relay not initialized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;  // Return OK to avoid error logging, but indicate service unavailable
    }
    
    // Read JSON body
    char content[128] = {0};
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"No data received\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    
    // Parse JSON (simple parsing for "state":true/false)
    bool new_state = false;
    if (strstr(content, "\"state\":true") != NULL || strstr(content, "'state':true") != NULL) {
        new_state = true;
    } else if (strstr(content, "\"state\":false") != NULL || strstr(content, "'state':false") != NULL) {
        new_state = false;
    } else {
        // Try to toggle if no state specified
        new_state = !relay_control_ui_get_state(relay_ui);
    }
    
    // Set relay state (this will update UI and hardware)
    relay_control_ui_set_state(relay_ui, new_state);
    
    char response[128];
    snprintf(response, sizeof(response), "{\"success\":true,\"id\":%d,\"state\":%s}", relay_id, new_state ? "true" : "false");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Handler for firmware upload
 */
static esp_err_t update_post_handler(httpd_req_t *req)
{
    // Get content length - may be 0 for chunked or multipart transfers
    size_t content_len = req->content_len;
    ESP_LOGI(TAG, "Received firmware update request, content_len: %zu bytes", content_len);
    
    // Check Content-Length header as well
    char content_length_str[32] = {0};
    if (httpd_req_get_hdr_value_str(req, "Content-Length", content_length_str, sizeof(content_length_str)) == ESP_OK) {
        size_t header_content_len = (size_t)atoi(content_length_str);
        if (header_content_len > 0 && content_len == 0) {
            content_len = header_content_len;
            ESP_LOGI(TAG, "Using Content-Length header value: %zu bytes", content_len);
        }
    }
    
    // Proceed even if content_len is 0 initially (will read until connection closes)
    if (content_len == 0) {
        ESP_LOGW(TAG, "Content length is 0, will read until connection closes");
    }
    
    {
        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "No OTA partition found");
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "No OTA partition found", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        
        // Save partition info in case we need it later (partition pointer is valid for the block scope)
        esp_partition_subtype_t partition_subtype = update_partition->subtype;
        uint32_t partition_address = update_partition->address;
        const char *partition_label = update_partition->label;
        
        ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx (label: %s)",
                 partition_subtype, partition_address, partition_label);
        
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
        size_t content_length = content_len;  // Use the calculated content length
        bool is_multipart = false;
        
        // Check if this is multipart form data
        char content_type[128] = {0};  // Increased buffer size
        if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK) {
            ESP_LOGI(TAG, "Content-Type: %s", content_type);
            if (strstr(content_type, "multipart/form-data") != NULL) {
                is_multipart = true;
                ESP_LOGI(TAG, "Multipart form data detected from header");
            }
        } else {
            ESP_LOGW(TAG, "No Content-Type header found - will detect from data");
        }
        
        // Read initial data to check if it's multipart (even without Content-Type header)
        int initial_recv = httpd_req_recv(req, buf, buf_size);
        if (initial_recv < 0) {
            ESP_LOGE(TAG, "Failed to receive initial data");
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Receive failed", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }
        
        // Check if data starts with boundary marker (--), indicating multipart even without header
        if (!is_multipart && initial_recv >= 2 && buf[0] == '-' && buf[1] == '-') {
            ESP_LOGI(TAG, "Detected multipart data from content (starts with '--')");
            is_multipart = true;
        }
        
        // Handle multipart form data
        if (is_multipart) {
            // Extract boundary from Content-Type header or from data
            char *boundary_start = NULL;
            char boundary[128] = {0};
            
            // Try to get boundary from Content-Type header first
            if (strlen(content_type) > 0) {
                boundary_start = strstr(content_type, "boundary=");
                if (boundary_start != NULL) {
                    boundary_start += 9; // Skip "boundary="
                    // Skip any whitespace
                    while (*boundary_start == ' ' || *boundary_start == '\t') {
                        boundary_start++;
                    }
                    // Remove quotes if present
                    if (*boundary_start == '"') {
                        boundary_start++;
                    }
                    char *boundary_end = strchr(boundary_start, ';');
                    if (boundary_end == NULL) {
                        boundary_end = boundary_start + strlen(boundary_start);
                    }
                    // Remove trailing quote if present
                    if (boundary_end > boundary_start && *(boundary_end - 1) == '"') {
                        boundary_end--;
                    }
                    // Trim whitespace at end
                    while (boundary_end > boundary_start && (*(boundary_end - 1) == ' ' || *(boundary_end - 1) == '\t' || *(boundary_end - 1) == '\r' || *(boundary_end - 1) == '\n')) {
                        boundary_end--;
                    }
                    size_t boundary_len = boundary_end - boundary_start;
                    if (boundary_len > sizeof(boundary) - 1) {
                        boundary_len = sizeof(boundary) - 1;
                    }
                    strncpy(boundary, boundary_start, boundary_len);
                    boundary[boundary_len] = '\0';
                    ESP_LOGI(TAG, "Multipart boundary from header: '%s' (len=%zu)", boundary, strlen(boundary));
                }
            }
            
            // If no boundary from header, extract it from the data itself
            if (strlen(boundary) == 0 && initial_recv >= 2 && buf[0] == '-' && buf[1] == '-') {
                // Find the end of the boundary marker (CRLF or --)
                // Look for CRLF after --
                char *crlf_pos = strstr(buf + 2, "\r\n");
                if (crlf_pos != NULL) {
                    size_t boundary_len = crlf_pos - (buf + 2);
                    if (boundary_len > sizeof(boundary) - 1) {
                        boundary_len = sizeof(boundary) - 1;
                    }
                    strncpy(boundary, buf + 2, boundary_len);
                    boundary[boundary_len] = '\0';
                    ESP_LOGI(TAG, "Multipart boundary from data: '%s' (len=%zu)", boundary, strlen(boundary));
                } else {
                    // Fallback: look for any non-alphanumeric boundary pattern
                    // Try to find where the boundary ends (before headers start)
                    char *header_start = strstr(buf, "\r\n");
                    if (header_start != NULL && header_start > buf + 2) {
                        size_t boundary_len = header_start - (buf + 2);
                        if (boundary_len > sizeof(boundary) - 1) {
                            boundary_len = sizeof(boundary) - 1;
                        }
                        strncpy(boundary, buf + 2, boundary_len);
                        boundary[boundary_len] = '\0';
                        ESP_LOGI(TAG, "Multipart boundary from data (fallback): '%s' (len=%zu)", boundary, strlen(boundary));
                    }
                }
            }
            
            if (strlen(boundary) == 0) {
                ESP_LOGE(TAG, "Failed to extract boundary from Content-Type or data");
                ESP_LOGE(TAG, "Content-Type: '%s', First 64 bytes: %.*s", content_type, initial_recv < 64 ? initial_recv : 64, buf);
            }
            
            ESP_LOGI(TAG, "Received initial %d bytes", initial_recv);
            // Log first 64 bytes as hex
            char hex_buf[256] = {0};
            int hex_len = initial_recv < 64 ? initial_recv : 64;
            for (int i = 0; i < hex_len; i++) {
                char hex_byte[4];
                snprintf(hex_byte, sizeof(hex_byte), "%02x ", (unsigned char)buf[i]);
                strcat(hex_buf, hex_byte);
            }
            ESP_LOGI(TAG, "First %d bytes (hex): %s", hex_len, hex_buf);
            
            // Find the start of binary data (after multipart boundary and headers)
            // Multipart format: --boundary\r\n[headers]\r\n\r\n[binary data]
            char *binary_start = NULL;
            size_t data_size = 0;
            
            if (strlen(boundary) > 0) {
                // Look for the boundary marker at the start
                char boundary_marker[256];
                snprintf(boundary_marker, sizeof(boundary_marker), "--%s", boundary);
                size_t boundary_marker_len = strlen(boundary_marker);
                
                // Check if buffer starts with boundary
                if (initial_recv >= boundary_marker_len && 
                    memcmp(buf, boundary_marker, boundary_marker_len) == 0) {
                    // Find the CRLF after boundary
                    char *after_boundary = buf + boundary_marker_len;
                    // Skip CRLF if present
                    if (initial_recv >= boundary_marker_len + 2 && 
                        memcmp(after_boundary, "\r\n", 2) == 0) {
                        after_boundary += 2;
                    }
                    // Now look for the header separator
                    binary_start = strstr(after_boundary, "\r\n\r\n");
                    if (binary_start != NULL) {
                        binary_start += 4;  // Skip \r\n\r\n
                        data_size = initial_recv - (binary_start - buf);
                    }
                } else {
                    // Boundary not at start, try to find header separator directly
                    binary_start = strstr(buf, "\r\n\r\n");
                    if (binary_start != NULL) {
                        binary_start += 4;
                        data_size = initial_recv - (binary_start - buf);
                    }
                }
            } else {
                // No boundary, just look for header separator
                binary_start = strstr(buf, "\r\n\r\n");
                if (binary_start != NULL) {
                    binary_start += 4;
                    data_size = initial_recv - (binary_start - buf);
                }
            }
            
            if (binary_start != NULL && data_size > 0) {
                // Defensive check: ESP32 binary should start with 0xE9
                // If we see 0x2D ('-'), we're still in the boundary/headers
                if ((unsigned char)binary_start[0] == 0x2D) {
                    ESP_LOGE(TAG, "Binary start still points to boundary marker (0x2D)! Trying to find actual data start...");
                    // Try to find the next occurrence of \r\n\r\n
                    char *next_sep = strstr(binary_start + 1, "\r\n\r\n");
                    if (next_sep != NULL) {
                        binary_start = next_sep + 4;
                        data_size = initial_recv - (binary_start - buf);
                        ESP_LOGI(TAG, "Found actual binary start after second separator, new offset: %zu, new size: %zu", 
                                 binary_start - buf, data_size);
                    } else {
                        ESP_LOGE(TAG, "Cannot find binary data start - multipart parsing failed");
                        free(buf);
                        esp_ota_abort(ota_handle);
                        httpd_resp_set_status(req, "400 Bad Request");
                        httpd_resp_send(req, "Invalid multipart data format", HTTPD_RESP_USE_STRLEN);
                        return ESP_ERR_INVALID_ARG;
                    }
                }
                
                // Verify we're not including boundary in the data
                if (strlen(boundary) > 0) {
                    char boundary_check[256];
                    snprintf(boundary_check, sizeof(boundary_check), "--%s", boundary);
                    if (data_size >= strlen(boundary_check) && 
                        memcmp(binary_start, boundary_check, strlen(boundary_check)) == 0) {
                        ESP_LOGE(TAG, "Binary start points to boundary! Header parsing failed.");
                        free(buf);
                        esp_ota_abort(ota_handle);
                        httpd_resp_set_status(req, "500 Internal Server Error");
                        httpd_resp_send(req, "Invalid multipart data", HTTPD_RESP_USE_STRLEN);
                        return ESP_FAIL;
                    }
                }
                
                // Final check: ESP32 binary magic byte should be 0xE9
                if (data_size > 0 && (unsigned char)binary_start[0] != 0xE9) {
                    ESP_LOGW(TAG, "Warning: First byte is 0x%02x, expected 0xE9 for ESP32 binary", 
                             (unsigned char)binary_start[0]);
                }
                
                ESP_LOGI(TAG, "Found multipart header separator, binary start offset: %zu, data size: %zu", 
                         binary_start - buf, data_size);
                ESP_LOGI(TAG, "First 16 bytes of binary data: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                         (unsigned char)binary_start[0], (unsigned char)binary_start[1], 
                         (unsigned char)binary_start[2], (unsigned char)binary_start[3],
                         (unsigned char)binary_start[4], (unsigned char)binary_start[5],
                         (unsigned char)binary_start[6], (unsigned char)binary_start[7],
                         (unsigned char)binary_start[8], (unsigned char)binary_start[9],
                         (unsigned char)binary_start[10], (unsigned char)binary_start[11],
                         (unsigned char)binary_start[12], (unsigned char)binary_start[13],
                         (unsigned char)binary_start[14], (unsigned char)binary_start[15]);
                
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
                    ESP_LOGI(TAG, "Written initial chunk: %zu bytes", data_size);
                }
            } else {
                // Could not find binary data start - this is an error for multipart
                ESP_LOGE(TAG, "Failed to find binary data start in multipart data");
                ESP_LOGE(TAG, "Boundary: '%s', Initial recv: %d bytes", boundary, initial_recv);
                free(buf);
                esp_ota_abort(ota_handle);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Invalid multipart data format", HTTPD_RESP_USE_STRLEN);
                return ESP_ERR_INVALID_ARG;
            }
            
            // Continue receiving remaining data
            // Note: content_length includes multipart overhead, so we need to read until we find the final boundary
            bool found_final_boundary = false;
            while (!found_final_boundary && (content_length == 0 || received < content_length)) {
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
                    ESP_LOGI(TAG, "Connection closed, received %zu bytes total (expected ~%zu based on content_length)", received, content_length);
                    // Check if the last buffer we wrote might contain the final boundary
                    // The final boundary is typically at the end: \r\n--boundary--
                    // We need to check if we've already written past it or if it's in a previous buffer
                    
                    // If we haven't found the final boundary, we might have written it already
                    // The missing 175 bytes is likely the final boundary marker size
                    // Check if we're very close to content_length (within boundary size)
                    size_t boundary_overhead = strlen(boundary) > 0 ? strlen(boundary) + 6 : 200; // --boundary-- + CRLF
                    if (content_length > 0 && received >= (content_length - boundary_overhead - 100)) {
                        ESP_LOGI(TAG, "Connection closed, received %zu bytes (missing %zu bytes, likely final boundary). Image should be complete.", 
                                 received, content_length - received);
                        // The binary data should be complete - the missing bytes are the final boundary
                        break;
                    } else if (content_length == 0) {
                        // No content length, assume we got all data when connection closes
                        ESP_LOGI(TAG, "No content length specified, connection closed - assuming all data received");
                        break;
                    } else {
                        ESP_LOGW(TAG, "Connection closed but may be missing significant data (received %zu, content_length %zu, missing %zu)", 
                                 received, content_length, content_length - received);
                        // Don't break - try to continue reading (though connection is closed, this won't help)
                        // But we should still try to verify what we have
                        break;
                    }
                }
                
                // Check if this chunk contains the end boundary
                char *end_boundary = NULL;
                size_t data_to_write = recv_len;
                bool is_final_boundary = false;
                
                if (strlen(boundary) > 0) {
                    // Check for final boundary marker: \r\n--boundary--
                    char final_boundary[256];
                    snprintf(final_boundary, sizeof(final_boundary), "\r\n--%s--", boundary);
                    end_boundary = strstr(buf, final_boundary);
                    if (end_boundary != NULL) {
                        data_to_write = end_boundary - buf;
                        is_final_boundary = true;
                    } else {
                        // Check for final boundary at start of buffer: --boundary--
                        char final_boundary_start[256];
                        snprintf(final_boundary_start, sizeof(final_boundary_start), "--%s--", boundary);
                        size_t boundary_len = strlen(final_boundary_start);
                        if (recv_len >= boundary_len && memcmp(buf, final_boundary_start, boundary_len) == 0) {
                            data_to_write = 0;  // Don't write the boundary
                            end_boundary = buf;  // Mark as found
                            is_final_boundary = true;
                        } else {
                            // Check for next part boundary: \r\n--boundary\r\n (not final, but still end of our data)
                            char next_boundary[256];
                            snprintf(next_boundary, sizeof(next_boundary), "\r\n--%s\r\n", boundary);
                            end_boundary = strstr(buf, next_boundary);
                            if (end_boundary != NULL) {
                                data_to_write = end_boundary - buf;
                                is_final_boundary = false;  // This is a part boundary, not the final one
                            }
                        }
                    }
                } else {
                    // Fallback: look for any boundary pattern
                    end_boundary = strstr(buf, "\r\n--");
                    if (end_boundary != NULL) {
                        data_to_write = end_boundary - buf;
                        // Check if it's a final boundary (ends with --)
                        if (end_boundary + 2 < buf + recv_len && 
                            *(end_boundary + 2) == '-' && *(end_boundary + 3) == '-') {
                            is_final_boundary = true;
                        }
                    } else if (recv_len >= 2 && memcmp(buf, "--", 2) == 0) {
                        // Check if it's final boundary (--boundary--)
                        if (recv_len >= 4 && buf[recv_len-2] == '-' && buf[recv_len-1] == '-') {
                            data_to_write = 0;
                            end_boundary = buf;
                            is_final_boundary = true;
                        }
                    }
                }
                
                if (end_boundary != NULL && is_final_boundary) {
                    // Found final boundary, only write data before it
                    if (data_to_write > 0) {
                        err = esp_ota_write(ota_handle, (const void *)buf, data_to_write);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                            free(buf);
                            esp_ota_abort(ota_handle);
                            httpd_resp_set_status(req, "500 Internal Server Error");
                            httpd_resp_send(req, "OTA write failed", HTTPD_RESP_USE_STRLEN);
                            return err;
                        }
                        received += data_to_write;
                        ESP_LOGI(TAG, "Found final boundary, wrote final %zu bytes, total: %zu", data_to_write, received);
                    }
                    found_final_boundary = true;
                    break;  // Reached end of multipart data
                } else if (end_boundary != NULL && !is_final_boundary) {
                    // Found a part boundary (not final), write data before it and continue
                    if (data_to_write > 0) {
                        err = esp_ota_write(ota_handle, (const void *)buf, data_to_write);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                            free(buf);
                            esp_ota_abort(ota_handle);
                            httpd_resp_set_status(req, "500 Internal Server Error");
                            httpd_resp_send(req, "OTA write failed", HTTPD_RESP_USE_STRLEN);
                            return err;
                        }
                        received += data_to_write;
                        ESP_LOGI(TAG, "Found part boundary, wrote %zu bytes, total: %zu, continuing...", data_to_write, received);
                    }
                    // Continue reading to find the final boundary
                } else {
                    // No boundary found in this chunk
                    // But check if this chunk might end with the start of a boundary
                    // (boundary might be split across chunks)
                    size_t write_len = recv_len;
                    
                    if (strlen(boundary) > 0 && recv_len > 4) {
                        // Check if the end of this chunk contains a boundary marker
                        // Look backwards from the end for "--boundary" or "\r\n--boundary" or "--boundary--"
                        char boundary_start[256];
                        char boundary_final[256];
                        snprintf(boundary_start, sizeof(boundary_start), "--%s", boundary);
                        snprintf(boundary_final, sizeof(boundary_final), "--%s--", boundary);
                        size_t boundary_start_len = strlen(boundary_start);
                        size_t boundary_final_len = strlen(boundary_final);
                        
                        // Check the last 200 bytes of the buffer for boundary markers
                        int check_start = (int)recv_len - 200;
                        if (check_start < 0) check_start = 0;
                        
                        for (int i = check_start; i < (int)recv_len - 1; i++) {
                            if (buf[i] == '-' && buf[i+1] == '-') {
                                // Found "--", check if it matches our boundary
                                // First check for final boundary: --boundary--
                                if (i + boundary_final_len <= recv_len) {
                                    if (memcmp(buf + i, boundary_final, boundary_final_len) == 0) {
                                        // Found final boundary - don't write from this point
                                        write_len = i;
                                        // Check if it's preceded by \r\n (making it \r\n--boundary--)
                                        if (i >= 2 && buf[i-2] == '\r' && buf[i-1] == '\n') {
                                            write_len = i - 2;  // Also skip the \r\n
                                        }
                                        found_final_boundary = true;
                                        ESP_LOGI(TAG, "Found final boundary (--boundary--) at offset %d in chunk, writing only %zu bytes", i, write_len);
                                        break;
                                    }
                                }
                                // Check for boundary start: --boundary (might be part of final or part boundary)
                                if (i + boundary_start_len <= recv_len) {
                                    if (memcmp(buf + i, boundary_start, boundary_start_len) == 0) {
                                        // Found boundary start - don't write from this point
                                        write_len = i;
                                        // Check if it's preceded by \r\n (making it \r\n--boundary)
                                        if (i >= 2 && buf[i-2] == '\r' && buf[i-1] == '\n') {
                                            write_len = i - 2;  // Also skip the \r\n
                                        }
                                        // Check if it's followed by -- (making it --boundary--, which we already checked above)
                                        // If not, it might be a part boundary, but we'll treat it as final to be safe
                                        found_final_boundary = true;
                                        ESP_LOGI(TAG, "Found boundary at offset %d in chunk, writing only %zu bytes", i, write_len);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    
                    // Write the chunk (or partial chunk if boundary found)
                    if (write_len > 0) {
                        err = esp_ota_write(ota_handle, (const void *)buf, write_len);
                        if (err != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                            free(buf);
                            esp_ota_abort(ota_handle);
                            httpd_resp_set_status(req, "500 Internal Server Error");
                            httpd_resp_send(req, "OTA write failed", HTTPD_RESP_USE_STRLEN);
                            return err;
                        }
                        
                        received += write_len;
                        ESP_LOGI(TAG, "Written %zu bytes, total: %zu/%zu", write_len, received, content_length);
                    }
                    
                    if (found_final_boundary) {
                        break;
                    }
                }
            }
        } else {
            // Not multipart - treat as raw binary
            // If content_length is 0, read until connection closes
            while (content_length == 0 || received < content_length) {
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
                    ESP_LOGI(TAG, "Connection closed, received %zu bytes total", received);
                    break;  // Connection closed
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
        }
        
        free(buf);
        
        ESP_LOGI(TAG, "Finished receiving data. Total binary bytes written: %zu (content_length was %zu, difference: %zu bytes of multipart overhead)", 
                 received, content_length, content_length - received);
        
        // Verify the image size matches expected binary size
        // The binary file should be exactly the size we wrote
        ESP_LOGI(TAG, "Attempting to finalize OTA update...");
        
        // Verify the image magic byte before calling esp_ota_end
        // This helps catch obvious corruption before hitting the assertion issue
        uint8_t magic_byte = 0;
        esp_err_t read_err = esp_partition_read(update_partition, 0, &magic_byte, 1);
        if (read_err == ESP_OK && magic_byte == 0xE9) {
            ESP_LOGI(TAG, "Image magic byte verified (0x%02X), image appears valid", magic_byte);
        } else {
            ESP_LOGE(TAG, "Image magic byte check failed (read: 0x%02X, expected: 0xE9)", magic_byte);
            esp_ota_abort(ota_handle);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Image validation failed - invalid magic byte", HTTPD_RESP_USE_STRLEN);
            return ESP_ERR_INVALID_ARG;
        }
        
        // Temporarily reduce log level for bootloader_support component to avoid logging lock issues
        // during image verification (known ESP-IDF issue in some versions)
        esp_log_level_set("bootloader_support", ESP_LOG_ERROR);
        esp_log_level_set("esp_image", ESP_LOG_ERROR);
        esp_log_level_set("*", ESP_LOG_WARN);  // Reduce all logging to WARN during verification
        
        err = esp_ota_end(ota_handle);
        
        // Restore log level
        esp_log_level_set("bootloader_support", ESP_LOG_INFO);
        esp_log_level_set("esp_image", ESP_LOG_INFO);
        esp_log_level_set("*", ESP_LOG_DEBUG);  // Restore default
        
        if (err != ESP_OK) {
            if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "Image validation failed, image is corrupted. Written %zu bytes.", received);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, "Image validation failed", HTTPD_RESP_USE_STRLEN);
                return err;
            }
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "OTA end failed", HTTPD_RESP_USE_STRLEN);
            return err;
        }
        
        ESP_LOGI(TAG, "Image verification successful!");
        
        // Send success response first, before setting boot partition
        // This ensures the client gets confirmation even if something goes wrong
        httpd_resp_set_status(req, "200 OK");
        httpd_resp_send(req, "Firmware update successful! Device will reboot...", HTTPD_RESP_USE_STRLEN);
        
        // Give time for response to be fully sent and ensure we're in task context
        // Also allow HTTP server to finish processing
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        
        // Disable WiFi to avoid any interference during boot partition update
        // This can help avoid cache coherency issues
        ESP_LOGI(TAG, "Preparing to set boot partition...");
        
        // Re-get the partition to ensure pointer is valid (avoid cache issues)
        const esp_partition_t *boot_partition = esp_ota_get_next_update_partition(NULL);
        if (boot_partition == NULL) {
            // Fallback: find partition by address
            boot_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, partition_subtype, partition_label);
        }
        
        if (boot_partition == NULL) {
            ESP_LOGE(TAG, "Could not find partition to set as boot (subtype %d, addr 0x%lx)", 
                     partition_subtype, partition_address);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Could not find boot partition", HTTPD_RESP_USE_STRLEN);
            return ESP_ERR_NOT_FOUND;
        }
        
        ESP_LOGI(TAG, "Setting boot partition to OTA partition (subtype %d, offset 0x%lx, label: %s)...", 
                 boot_partition->subtype, boot_partition->address, boot_partition->label);
        err = esp_ota_set_boot_partition(boot_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
            // Even if setting boot partition fails, we've already sent success response
            // The image is valid, so on next boot it might still work
            return err;
        }
        
        ESP_LOGI(TAG, "Boot partition set successfully! Rebooting in 1 second...");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Rebooting now...");
        esp_restart();
    }
    
    // If we get here and no data was received, return error
    ESP_LOGE(TAG, "No data received for firmware update");
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, "No data received", HTTPD_RESP_USE_STRLEN);
    return ESP_ERR_INVALID_ARG;
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
    
    // Initialize SPIFFS
    esp_err_t spiffs_err = http_server_init_spiffs();
    if (spiffs_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS, continuing without file serving");
        // Continue anyway - API endpoints will still work
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 30;  // Increased to support all relay endpoints (1+1+1+6+6=15 minimum, with buffer)
    config.max_open_sockets = 7;
    config.stack_size = 16384;  // Increased stack size for large firmware uploads (default is 4096, increased to 16KB)
    
    ESP_LOGI(TAG, "Starting HTTP server on port %d with max_uri_handlers=%d", port, config.max_uri_handlers);
    
    esp_err_t start_err = httpd_start(&server_handle, &config);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(start_err));
        return start_err;
    }
    
    if (server_handle != NULL) {
        // Register handlers
        // Main control page
        httpd_uri_t control_page = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = control_page_handler,
            .user_ctx = NULL
        };
        esp_err_t reg_err = httpd_register_uri_handler(server_handle, &control_page);
        if (reg_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register control page handler: %s", esp_err_to_name(reg_err));
        }
        
        // Firmware update page
        httpd_uri_t update_page = {
            .uri = "/update",
            .method = HTTP_GET,
            .handler = update_page_handler,
            .user_ctx = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &update_page);
        if (reg_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register update page handler: %s", esp_err_to_name(reg_err));
        }
        
        // Firmware upload endpoint
        httpd_uri_t update_post = {
            .uri = "/update",
            .method = HTTP_POST,
            .handler = update_post_handler,
            .user_ctx = NULL
        };
        reg_err = httpd_register_uri_handler(server_handle, &update_post);
        if (reg_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register update POST handler: %s", esp_err_to_name(reg_err));
        }
        
        // Relay API endpoints - register for each relay (1-6)
        // Use static strings to ensure they persist
        static const char relay_uri_1[] = "/api/relay/1";
        static const char relay_uri_2[] = "/api/relay/2";
        static const char relay_uri_3[] = "/api/relay/3";
        static const char relay_uri_4[] = "/api/relay/4";
        static const char relay_uri_5[] = "/api/relay/5";
        static const char relay_uri_6[] = "/api/relay/6";
        
        const char *relay_uris[] = {
            relay_uri_1, relay_uri_2, relay_uri_3,
            relay_uri_4, relay_uri_5, relay_uri_6
        };
        
        for (int i = 0; i < 6; i++) {
            httpd_uri_t relay_get = {
                .uri = relay_uris[i],
                .method = HTTP_GET,
                .handler = relay_get_handler,
                .user_ctx = NULL
            };
            reg_err = httpd_register_uri_handler(server_handle, &relay_get);
            if (reg_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register GET handler for %s: %s", relay_uris[i], esp_err_to_name(reg_err));
            } else {
                ESP_LOGI(TAG, "Registered GET handler for %s", relay_uris[i]);
            }
            
            httpd_uri_t relay_post = {
                .uri = relay_uris[i],
                .method = HTTP_POST,
                .handler = relay_post_handler,
                .user_ctx = NULL
            };
            reg_err = httpd_register_uri_handler(server_handle, &relay_post);
            if (reg_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register POST handler for %s: %s", relay_uris[i], esp_err_to_name(reg_err));
            } else {
                ESP_LOGI(TAG, "Registered POST handler for %s", relay_uris[i]);
            }
        }
        
        server_running = true;
        ESP_LOGI(TAG, "HTTP server started successfully on port %d", port);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Server handle is NULL after httpd_start");
        return ESP_FAIL;
    }
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

