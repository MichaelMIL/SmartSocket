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

// Moving-average window for the current reading (number of readings averaged)
#define AMPS_MA_WINDOW 10

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
    float cal_zero_v;           // Calibration: sensor output voltage at 0 A
    float cal_gain;             // Calibration: A per volt of deviation from zero
    float ma_buf[AMPS_MA_WINDOW]; // Moving-average ring buffer of recent readings
    uint8_t ma_head;            // Next write index into ma_buf
    uint8_t ma_count;           // Number of valid entries in ma_buf (<= AMPS_MA_WINDOW)
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
 * The value is smoothed across successive readings (trimmed-mean sampling
 * plus an exponential moving average) to suppress ADC noise.
 *
 * @param hw Pointer to the relay hardware object
 * @return float Current in Amperes, or 0.0 if ADC not configured or error
 */
float relay_hardware_read_current(relay_hardware_t *hw);

/**
 * @brief Read the averaged raw sensor output voltage (before calibration math)
 *
 * @param hw Pointer to the relay hardware object
 * @return float Voltage in Volts, or -1.0 if ADC not configured or error
 */
float relay_hardware_read_raw_voltage(const relay_hardware_t *hw);

/**
 * @brief Calibrate the zero point - call with NO load connected (0 A flowing)
 *
 * Samples the sensor and stores its output voltage as the 0 A reference.
 * Persisted to NVS per ADC channel.
 *
 * @param hw Pointer to the relay hardware object
 * @return esp_err_t ESP_OK on success
 */
esp_err_t relay_hardware_calibrate_zero(relay_hardware_t *hw);

/**
 * @brief Calibrate the gain - call with a known load measured by an amp meter
 *
 * Samples the sensor and computes gain so the reading matches actual_amps.
 * Calibrate the zero point first. Persisted to NVS per ADC channel.
 *
 * @param hw Pointer to the relay hardware object
 * @param actual_amps Current actually flowing, as read on the amp meter (> 0)
 * @return esp_err_t ESP_OK on success, ESP_ERR_INVALID_STATE if the sensor
 *         deviation from zero is too small to derive a gain
 */
esp_err_t relay_hardware_calibrate_gain(relay_hardware_t *hw, float actual_amps);

/**
 * @brief Reset calibration to ACS712 datasheet defaults and erase saved values
 *
 * @param hw Pointer to the relay hardware object
 * @return esp_err_t ESP_OK on success
 */
esp_err_t relay_hardware_reset_calibration(relay_hardware_t *hw);

/**
 * @brief Get current calibration values
 *
 * @param hw Pointer to the relay hardware object
 * @param zero_volts Receives the 0 A reference voltage (may be NULL)
 * @param gain_a_per_v Receives the gain in A/V (may be NULL)
 */
void relay_hardware_get_calibration(const relay_hardware_t *hw, float *zero_volts, float *gain_a_per_v);

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

