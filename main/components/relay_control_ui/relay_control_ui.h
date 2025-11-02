/*
 * Relay Control UI Component Header
 * 
 * Provides a UI component for controlling a relay with a color-changing button.
 * Uses an object-oriented approach with a struct to encapsulate state.
 */

#ifndef RELAY_CONTROL_UI_H
#define RELAY_CONTROL_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "esp_timer.h"
#include "relay_hardware.h"

#ifdef __cplusplus
extern "C" {
#endif

// #define RELAY_TIMER_DURATION_SECONDS (30 * 60)  // 30 minutes in seconds
#define RELAY_TIMER_DURATION_SECONDS (10)  // 10 seconds in seconds
#define BUTTON_WIDTH_PX 100
#define BUTTON_HEIGHT_PX 60
#define BUTTON_OFF_COLOR lv_color_hex(0xC00000)
#define BUTTON_ON_COLOR lv_color_hex(0x00C000)
#define TIMER_LABEL_HEIGHT_PX 20
#define TIMER_LABEL_WIDTH_PX 100
#define TIMER_LABEL_X_OFFSET_PX 0
#define TIMER_LABEL_Y_OFFSET_PX 20
#define TIMER_LABEL_TEXT_COLOR lv_color_hex(0xFFFFFF)
#define TIMER_LABEL_TEXT_ALIGN LV_TEXT_ALIGN_CENTER
#define PROGRESS_BAR_WIDTH_PX 80
#define PROGRESS_BAR_HEIGHT_PX 4
#define PROGRESS_BAR_Y_OFFSET_PX -10
#define PROGRESS_BAR_X_OFFSET_PX 90


/**
 * @brief Relay Control UI object structure
 */
// Forward declaration
typedef struct relay_control_ui_s relay_control_ui_t;

/**
 * @brief Callback function type for state change notifications
 */
typedef void (*relay_state_change_cb_t)(relay_control_ui_t *ui, bool new_state);

/**
 * @brief Relay Control UI object structure
 */
struct relay_control_ui_s {
    lv_obj_t *button;          // The button object
    lv_obj_t *label;           // The label inside the button
    lv_obj_t *timer_label;     // The timer countdown label
    lv_obj_t *progress_bar;    // The animated progress bar for countdown
    lv_timer_t *lvgl_timer;    // LVGL timer for safe UI updates
    bool state;                 // Current relay state (true = ON, false = OFF)
    const char *tag;            // Log tag for this instance
    const char *name;           // Display name for this relay (e.g., "Relay 1")
    esp_timer_handle_t timer;   // Timer handle for countdown
    uint32_t time_remaining;   // Time remaining in seconds
    volatile bool update_needed; // Flag to signal UI update needed (set from timer callback)
    bool long_press_active;    // Flag to track if long press just happened (prevents CLICKED event from toggling)
    relay_state_change_cb_t state_change_cb; // Callback when state changes
    void *state_change_cb_arg;  // User data for state change callback
    relay_hardware_t *hardware;  // Pointer to hardware control object (NULL if no hardware)
};

/**
 * @brief Create a new relay control UI object
 * 
 * @param parent Parent object (usually the screen)
 * @param tag Log tag for this instance (can be NULL for default)
 * @param name Display name for the relay (e.g., "Relay 1", "Socket A") - will appear as "{name} ON/OFF"
 * @param align Alignment type (e.g., LV_ALIGN_CENTER, LV_ALIGN_TOP_LEFT, etc.)
 * @param x_offset X offset from alignment point
 * @param y_offset Y offset from alignment point
 * @param hardware Pointer to relay hardware object (can be NULL for UI-only, no hardware control)
 * @return relay_control_ui_t* Pointer to the created relay control UI object, or NULL on failure
 */
relay_control_ui_t *relay_control_ui_create(lv_obj_t *parent, const char *tag, const char *name, lv_align_t align, int16_t x_offset, int16_t y_offset, relay_hardware_t *hardware);

/**
 * @brief Delete/destroy a relay control UI object
 * 
 * @param ui Pointer to the relay control UI object to destroy
 */
void relay_control_ui_delete(relay_control_ui_t *ui);

/**
 * @brief Get current relay state
 * 
 * @param ui Pointer to the relay control UI object
 * @return true if relay is ON, false if OFF
 */
bool relay_control_ui_get_state(const relay_control_ui_t *ui);

/**
 * @brief Set relay state programmatically
 * 
 * @param ui Pointer to the relay control UI object
 * @param state true for ON, false for OFF
 */
void relay_control_ui_set_state(relay_control_ui_t *ui, bool state);

/**
 * @brief Toggle relay state programmatically
 * 
 * @param ui Pointer to the relay control UI object
 */
void relay_control_ui_toggle(relay_control_ui_t *ui);

/**
 * @brief Get the button object (for advanced customization)
 * 
 * @param ui Pointer to the relay control UI object
 * @return lv_obj_t* Pointer to the button object
 */
lv_obj_t *relay_control_ui_get_button(relay_control_ui_t *ui);

/**
 * @brief Set state change callback
 * 
 * @param ui Pointer to the relay control UI object
 * @param cb Callback function (can be NULL)
 * @param arg User data for callback
 */
void relay_control_ui_set_state_change_callback(relay_control_ui_t *ui, relay_state_change_cb_t cb, void *arg);

#ifdef __cplusplus
}
#endif

#endif // RELAY_CONTROL_UI_H

