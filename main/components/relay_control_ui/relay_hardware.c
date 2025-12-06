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
#include <math.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *DEFAULT_TAG = "relay_hw";

// ADC configuration constants for ACS712
#define ADC_ATTEN           ADC_ATTEN_DB_12  // 0-3.3V range (DB_11 deprecated, DB_12 is equivalent)
#define ADC_BITWIDTH        ADC_BITWIDTH_12  // 12-bit resolution
#define ADC_SAMPLE_COUNT    64               // Number of samples for averaging
#define ACS712_VCC          3.3f             // Supply voltage (adjust if using 5V)
#define ACS712_VREF         1.65f            // Reference voltage (VCC/2 for ACS712)
#define ACS712_SENSITIVITY  0.066f           // Sensitivity in V/A (for ACS712-5A: 185mV/A = 0.185V/A, adjust for your model)
// For ACS712-5A: 185mV/A, for ACS712-20A: 100mV/A, for ACS712-30A: 66mV/A
// For ESP32-S3, ADC1 channels are 0-9, so we use ADC_CHANNEL_9 + 1 as invalid marker
#define ADC_CHANNEL_INVALID (ADC_CHANNEL_9 + 1)

// Global ADC handles (shared across all relay hardware instances)
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_oneshot_unit_handle_t adc2_handle = NULL;
static bool adc1_initialized = false;
static bool adc2_initialized = false;

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
    
    // Control LED indicator (LED ON when relay ON, LED OFF when relay OFF)
    if (hw->led_pin >= 0 && hw->led_pin != GPIO_NUM_NC) {
        // LED: HIGH = ON, LOW = OFF (active HIGH LED)
        gpio_set_level(hw->led_pin, state ? 1 : 0);
    }
    
    // Safe logging - check tag pointer before use to prevent crash
    const char *tag = (hw->tag != NULL) ? hw->tag : DEFAULT_TAG;
    ESP_LOGI(tag, "Relay GPIO %d set to %s, LED GPIO %d set to %s", 
             hw->gpio_pin, state ? "ON" : "OFF",
             hw->led_pin, state ? "ON" : "OFF");
    
    return ESP_OK;
}

/**
 * @brief Initialize ADC unit for current sensing
 * 
 * @param unit_id ADC unit ID (ADC_UNIT_1 or ADC_UNIT_2)
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t init_adc_unit(adc_unit_t unit_id)
{
    if (unit_id == ADC_UNIT_1) {
        if (adc1_initialized) {
            return ESP_OK;  // Already initialized
        }
        
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
        };
        esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(DEFAULT_TAG, "Failed to initialize ADC1: %s", esp_err_to_name(ret));
            return ret;
        }
        
        adc1_initialized = true;
        ESP_LOGI(DEFAULT_TAG, "ADC1 initialized for current sensing");
        return ESP_OK;
    } else if (unit_id == ADC_UNIT_2) {
        if (adc2_initialized) {
            return ESP_OK;  // Already initialized
        }
        
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_2,
        };
        esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc2_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(DEFAULT_TAG, "Failed to initialize ADC2: %s", esp_err_to_name(ret));
            return ret;
        }
        
        adc2_initialized = true;
        ESP_LOGI(DEFAULT_TAG, "ADC2 initialized for current sensing");
        return ESP_OK;
    }
    
    return ESP_ERR_INVALID_ARG;
}

/**
 * @brief Configure ADC channel for a specific relay
 * 
 * @param hw Pointer to the relay hardware object
 * @return esp_err_t ESP_OK on success
 */
static esp_err_t init_adc_channel(relay_hardware_t *hw)
{
    if (hw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // If ADC channel is not configured, skip initialization
    // For ESP32-S3, valid ADC channels are 0-9
    if (hw->adc_channel > ADC_CHANNEL_9) {
        return ESP_OK;  // Not an error, just no ADC configured
    }
    
    // Initialize ADC unit if not already done
    esp_err_t ret = init_adc_unit(hw->adc_unit);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Get the appropriate ADC handle
    adc_oneshot_unit_handle_t adc_handle = NULL;
    if (hw->adc_unit == ADC_UNIT_1) {
        adc_handle = adc1_handle;
    } else if (hw->adc_unit == ADC_UNIT_2) {
        adc_handle = adc2_handle;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (adc_handle == NULL) {
        const char *tag = (hw->tag != NULL) ? hw->tag : DEFAULT_TAG;
        ESP_LOGE(tag, "ADC handle is NULL for unit %d", hw->adc_unit);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Configure ADC channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH,
        .atten = ADC_ATTEN,
    };
    
    ret = adc_oneshot_config_channel(adc_handle, hw->adc_channel, &config);
    if (ret != ESP_OK) {
        const char *tag = (hw->tag != NULL) ? hw->tag : DEFAULT_TAG;
        ESP_LOGE(tag, "Failed to configure ADC%d channel %d: %s", hw->adc_unit, hw->adc_channel, esp_err_to_name(ret));
        return ret;
    }
    
    const char *tag = (hw->tag != NULL) ? hw->tag : DEFAULT_TAG;
    ESP_LOGI(tag, "ADC%d channel %d configured for current sensing", hw->adc_unit, hw->adc_channel);
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
    
    const char *tag = (hw->tag != NULL) ? hw->tag : DEFAULT_TAG;
    uint64_t pin_mask = 0;
    
    // Configure relay GPIO pin as output
    if (hw->gpio_pin >= 0 && hw->gpio_pin != GPIO_NUM_NC) {
        pin_mask |= (1ULL << hw->gpio_pin);
    }
    
    // Configure LED GPIO pin as output
    if (hw->led_pin >= 0 && hw->led_pin != GPIO_NUM_NC) {
        pin_mask |= (1ULL << hw->led_pin);
    }
    
    // If no GPIO pins configured, skip initialization
    if (pin_mask == 0) {
        ESP_LOGI(tag, "No GPIO pins configured, skipping hardware initialization");
        return ESP_OK;
    }
    
    // Configure GPIO pins as output
    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to configure GPIO pins: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize relay to OFF state (HIGH for active LOW relay)
    if (hw->gpio_pin >= 0 && hw->gpio_pin != GPIO_NUM_NC) {
        gpio_set_level(hw->gpio_pin, 1);
        ESP_LOGI(tag, "Relay GPIO %d initialized as output, set to OFF", hw->gpio_pin);
    }
    
    // Initialize LED to OFF state (LOW for active HIGH LED)
    if (hw->led_pin >= 0 && hw->led_pin != GPIO_NUM_NC) {
        gpio_set_level(hw->led_pin, 0);
        ESP_LOGI(tag, "LED GPIO %d initialized as output, set to OFF", hw->led_pin);
    }
    
    return ESP_OK;
}

/**
 * @brief Create and initialize a relay hardware object
 * 
 * @param gpio_pin GPIO pin number for controlling the relay hardware (use GPIO_NUM_NC for no hardware control)
 * @param led_pin GPIO pin number for LED indicator (use GPIO_NUM_NC for no LED)
 * @param adc_unit ADC unit (ADC_UNIT_1 or ADC_UNIT_2, use ADC_UNIT_1 for no current sensing)
 * @param adc_channel ADC channel for current sensing via ACS712 (use a value > ADC_CHANNEL_9 for no current sensing)
 * @param tag Log tag for this instance (can be NULL for default)
 * @return relay_hardware_t* Pointer to the created relay hardware object, or NULL on failure
 */
relay_hardware_t *relay_hardware_create(gpio_num_t gpio_pin, gpio_num_t led_pin, adc_unit_t adc_unit, adc_channel_t adc_channel, const char *tag)
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
    hw->led_pin = led_pin;
    hw->adc_unit = adc_unit;
    hw->adc_channel = adc_channel;
    hw->tag = (tag != NULL) ? tag : DEFAULT_TAG;
    hw->state = false;  // Start with relay OFF

    // Initialize GPIO pins for relay and LED control
    esp_err_t gpio_ret = init_relay_gpio(hw);
    if (gpio_ret != ESP_OK) {
        ESP_LOGW(hw->tag, "GPIO initialization failed, continuing without hardware control");
    }
    
    // Initialize ADC channel for current sensing
    esp_err_t adc_ret = init_adc_channel(hw);
    if (adc_ret != ESP_OK) {
        ESP_LOGW(hw->tag, "ADC initialization failed, continuing without current sensing");
    }
    
    ESP_LOGI(hw->tag, "Relay hardware object created for GPIO %d, LED GPIO %d, ADC%d channel %d", 
             gpio_pin, led_pin, adc_unit, adc_channel);
    
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
    
    // Set LED to OFF before deletion
    if (hw->led_pin >= 0 && hw->led_pin != GPIO_NUM_NC) {
        gpio_set_level(hw->led_pin, 0);  // OFF state for active HIGH LED
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

/**
 * @brief Get LED GPIO pin number
 * 
 * @param hw Pointer to the relay hardware object
 * @return gpio_num_t LED GPIO pin number, or GPIO_NUM_NC if not configured
 */
gpio_num_t relay_hardware_get_led_pin(const relay_hardware_t *hw)
{
    if (hw == NULL) {
        return GPIO_NUM_NC;
    }
    return hw->led_pin;
}

/**
 * @brief Read current consumption in Amperes from ACS712 sensor
 * 
 * @param hw Pointer to the relay hardware object
 * @return float Current in Amperes, or 0.0 if ADC not configured or error
 */
float relay_hardware_read_current(const relay_hardware_t *hw)
{
    if (hw == NULL) {
        return 0.0f;
    }
    
    // If ADC channel is not configured, return 0
    // For ESP32-S3, valid ADC channels are 0-9
    if (hw->adc_channel > ADC_CHANNEL_9) {
        return 0.0f;
    }
    
    // Get the appropriate ADC handle
    adc_oneshot_unit_handle_t adc_handle = NULL;
    bool adc_initialized = false;
    
    if (hw->adc_unit == ADC_UNIT_1) {
        adc_handle = adc1_handle;
        adc_initialized = adc1_initialized;
    } else if (hw->adc_unit == ADC_UNIT_2) {
        adc_handle = adc2_handle;
        adc_initialized = adc2_initialized;
    } else {
        return 0.0f;
    }
    
    if (!adc_initialized || adc_handle == NULL) {
        return 0.0f;
    }
    
    // Read multiple samples and average for better accuracy
    int32_t adc_sum = 0;
    int adc_raw = 0;
    
    for (int i = 0; i < ADC_SAMPLE_COUNT; i++) {
        esp_err_t ret = adc_oneshot_read(adc_handle, hw->adc_channel, &adc_raw);
        if (ret == ESP_OK) {
            adc_sum += adc_raw;
        }
    }
    
    // Calculate average ADC value
    float adc_avg = (float)adc_sum / ADC_SAMPLE_COUNT;
    
    // Convert ADC reading to voltage (0-4095 for 12-bit, 0-3.3V)
    float voltage = (adc_avg / 4095.0f) * ACS712_VCC;
    
    // Calculate current: ACS712 outputs VCC/2 (1.65V) at 0A
    // Current = (voltage - VREF) / sensitivity
    float current = (voltage - ACS712_VREF) / ACS712_SENSITIVITY;
    
    // Handle negative current (if sensor is reversed) by taking absolute value
    // You may want to adjust this based on your wiring
    if (current < 0.0f) {
        current = -current;
    }
    
    // Filter out noise (current below 0.1A is considered noise)
    if (current < 0.1f) {
        current = 0.0f;
    }
    
    return current;
}

/**
 * @brief Get ADC unit
 * 
 * @param hw Pointer to the relay hardware object
 * @return adc_unit_t ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
 */
adc_unit_t relay_hardware_get_adc_unit(const relay_hardware_t *hw)
{
    if (hw == NULL) {
        return ADC_UNIT_1;  // Default to ADC_UNIT_1
    }
    return hw->adc_unit;
}

/**
 * @brief Get ADC channel number
 * 
 * @param hw Pointer to the relay hardware object
 * @return adc_channel_t ADC channel number, or ADC_CHANNEL_INVALID if not configured
 */
adc_channel_t relay_hardware_get_adc_channel(const relay_hardware_t *hw)
{
    if (hw == NULL) {
        return ADC_CHANNEL_INVALID;
    }
    return hw->adc_channel;
}

