/*
 * Relay Hardware Control Component Header
 * 
 * Provides hardware abstraction for controlling physical relay hardware via GPIO.
 * Separated from UI to allow reuse and better separation of concerns.
 */

#ifndef RELAY_HARDWARE_H
#define RELAY_HARDWARE_H

#include <stdbool.h>
#include "driver/gpio.h"
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Relay hardware object structure
 */
typedef struct {
    gpio_num_t gpio_pin;        // GPIO pin number for controlling the relay
    gpio_num_t led_pin;         // GPIO pin number for LED indicator (use GPIO_NUM_NC for no LED)
    adc_unit_t adc_unit;        // ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
    adc_channel_t adc_channel;  // ADC channel for current sensing (ACS712)
    bool state;                 // Current relay state (true = ON, false = OFF)
    const char *tag;            // Log tag for this instance
} relay_hardware_t;

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
relay_hardware_t *relay_hardware_create(gpio_num_t gpio_pin, gpio_num_t led_pin, adc_unit_t adc_unit, adc_channel_t adc_channel, const char *tag);

/**
 * @brief Delete/destroy a relay hardware object
 * 
 * @param hw Pointer to the relay hardware object to destroy
 */
void relay_hardware_delete(relay_hardware_t *hw);

/**
 * @brief Get current relay state
 * 
 * @param hw Pointer to the relay hardware object
 * @return true if relay is ON, false if OFF
 */
bool relay_hardware_get_state(const relay_hardware_t *hw);

/**
 * @brief Set relay state (control GPIO pin)
 * 
 * @param hw Pointer to the relay hardware object
 * @param state true for ON, false for OFF
 * @return esp_err_t ESP_OK on success
 */
esp_err_t relay_hardware_set_state(relay_hardware_t *hw, bool state);

/**
 * @brief Toggle relay state
 * 
 * @param hw Pointer to the relay hardware object
 * @return esp_err_t ESP_OK on success
 */
esp_err_t relay_hardware_toggle(relay_hardware_t *hw);

/**
 * @brief Get GPIO pin number
 * 
 * @param hw Pointer to the relay hardware object
 * @return gpio_num_t GPIO pin number, or GPIO_NUM_NC if not configured
 */
gpio_num_t relay_hardware_get_gpio_pin(const relay_hardware_t *hw);

/**
 * @brief Get LED GPIO pin number
 * 
 * @param hw Pointer to the relay hardware object
 * @return gpio_num_t LED GPIO pin number, or GPIO_NUM_NC if not configured
 */
gpio_num_t relay_hardware_get_led_pin(const relay_hardware_t *hw);

/**
 * @brief Read current consumption in Amperes from ACS712 sensor
 * 
 * @param hw Pointer to the relay hardware object
 * @return float Current in Amperes, or 0.0 if ADC not configured or error
 */
float relay_hardware_read_current(const relay_hardware_t *hw);

/**
 * @brief Get ADC unit
 * 
 * @param hw Pointer to the relay hardware object
 * @return adc_unit_t ADC unit (ADC_UNIT_1 or ADC_UNIT_2)
 */
adc_unit_t relay_hardware_get_adc_unit(const relay_hardware_t *hw);

/**
 * @brief Get ADC channel number
 * 
 * @param hw Pointer to the relay hardware object
 * @return adc_channel_t ADC channel number, or ADC_CHANNEL_INVALID if not configured
 */
adc_channel_t relay_hardware_get_adc_channel(const relay_hardware_t *hw);

#ifdef __cplusplus
}
#endif

#endif // RELAY_HARDWARE_H

