/*
 * Relay Hardware Control Component
 * 
 * Provides hardware abstraction for controlling physical relay hardware via GPIO.
 * Separated from UI to allow reuse and better separation of concerns.
 */

#include "relay_hardware.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"

static const char *DEFAULT_TAG = "relay_hw";

/**
 * @brief Control the relay GPIO pin based on state
 * 
 * @param hw Pointer to the relay hardware object
 * @param state true = ON (GPIO LOW for active LOW relay), false = OFF (GPIO HIGH)
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t control_relay_gpio(relay_hardware_t *hw, bool state)
{
    if (hw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // If GPIO pin is not configured (GPIO_NUM_NC or -1), skip hardware control
    if (hw->gpio_pin < 0 || hw->gpio_pin == GPIO_NUM_NC) {
        return ESP_OK;  // Not an error, just no hardware connected
    }
    
    // Set GPIO level: LOW = ON, HIGH = OFF (active LOW relay)
    // Note: Adjust this based on your relay module (some are active HIGH)
    gpio_set_level(hw->gpio_pin, state ? 0 : 1);
    
    // Safe logging - check tag pointer before use to prevent crash
    const char *tag = (hw->tag != NULL) ? hw->tag : DEFAULT_TAG;
    ESP_LOGI(tag, "Relay GPIO %d set to %s", hw->gpio_pin, state ? "ON" : "OFF");
    
    return ESP_OK;
}

/**
 * @brief Initialize and configure the relay GPIO pin
 * 
 * @param hw Pointer to the relay hardware object
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t init_relay_gpio(relay_hardware_t *hw)
{
    if (hw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // If GPIO pin is not configured (GPIO_NUM_NC or -1), skip initialization
    if (hw->gpio_pin < 0 || hw->gpio_pin == GPIO_NUM_NC) {
        const char *tag = (hw->tag != NULL) ? hw->tag : DEFAULT_TAG;
        ESP_LOGI(tag, "GPIO pin not configured, skipping hardware initialization");
        return ESP_OK;
    }
    
    // Configure GPIO pin as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << hw->gpio_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    const char *tag = (hw->tag != NULL) ? hw->tag : DEFAULT_TAG;
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to configure GPIO %d: %s", hw->gpio_pin, esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize relay to OFF state (HIGH for active LOW relay)
    gpio_set_level(hw->gpio_pin, 1);
    ESP_LOGI(tag, "Relay GPIO %d initialized as output, set to OFF", hw->gpio_pin);
    
    return ESP_OK;
}

/**
 * @brief Create and initialize a relay hardware object
 * 
 * @param gpio_pin GPIO pin number for controlling the relay hardware (use GPIO_NUM_NC for no hardware control)
 * @param tag Log tag for this instance (can be NULL for default)
 * @return relay_hardware_t* Pointer to the created relay hardware object, or NULL on failure
 */
relay_hardware_t *relay_hardware_create(gpio_num_t gpio_pin, const char *tag)
{
    // Allocate memory for the object
    relay_hardware_t *hw = (relay_hardware_t *)malloc(sizeof(relay_hardware_t));
    if (hw == NULL) {
        ESP_LOGE(DEFAULT_TAG, "Failed to allocate memory for relay hardware");
        return NULL;
    }

    // Initialize the struct
    memset(hw, 0, sizeof(relay_hardware_t));
    hw->gpio_pin = gpio_pin;
    hw->tag = (tag != NULL) ? tag : DEFAULT_TAG;
    hw->state = false;  // Start with relay OFF

    // Initialize GPIO pin for relay control
    esp_err_t gpio_ret = init_relay_gpio(hw);
    if (gpio_ret != ESP_OK) {
        ESP_LOGW(hw->tag, "GPIO initialization failed, continuing without hardware control");
    }
    
    ESP_LOGI(hw->tag, "Relay hardware object created for GPIO %d", gpio_pin);
    
    return hw;
}

/**
 * @brief Delete/destroy a relay hardware object
 * 
 * @param hw Pointer to the relay hardware object to destroy
 */
void relay_hardware_delete(relay_hardware_t *hw)
{
    if (hw == NULL) {
        return;
    }
    
    // Set relay to OFF before deletion (safety)
    if (hw->gpio_pin >= 0 && hw->gpio_pin != GPIO_NUM_NC) {
        gpio_set_level(hw->gpio_pin, 1);  // OFF state for active LOW relay
    }
    
    // Free the object
    free(hw);
}

/**
 * @brief Get current relay state
 * 
 * @param hw Pointer to the relay hardware object
 * @return true if relay is ON, false if OFF
 */
bool relay_hardware_get_state(const relay_hardware_t *hw)
{
    if (hw == NULL) {
        return false;
    }
    return hw->state;
}

/**
 * @brief Set relay state (control GPIO pin)
 * 
 * @param hw Pointer to the relay hardware object
 * @param state true for ON, false for OFF
 * @return esp_err_t ESP_OK on success
 */
esp_err_t relay_hardware_set_state(relay_hardware_t *hw, bool state)
{
    if (hw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = control_relay_gpio(hw, state);
    if (ret == ESP_OK) {
        hw->state = state;
    }
    
    return ret;
}

/**
 * @brief Toggle relay state
 * 
 * @param hw Pointer to the relay hardware object
 * @return esp_err_t ESP_OK on success
 */
esp_err_t relay_hardware_toggle(relay_hardware_t *hw)
{
    if (hw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return relay_hardware_set_state(hw, !hw->state);
}

/**
 * @brief Get GPIO pin number
 * 
 * @param hw Pointer to the relay hardware object
 * @return gpio_num_t GPIO pin number, or GPIO_NUM_NC if not configured
 */
gpio_num_t relay_hardware_get_gpio_pin(const relay_hardware_t *hw)
{
    if (hw == NULL) {
        return GPIO_NUM_NC;
    }
    return hw->gpio_pin;
}

