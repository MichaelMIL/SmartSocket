/*
 * Relay Control UI Component
 * 
 * Provides a UI component for controlling a relay with a color-changing button.
 * Red = OFF, Green = ON
 * Uses an object-oriented approach with a struct to encapsulate state.
 */

#include "relay_control_ui.h"
#include "relay_hardware.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *DEFAULT_TAG = "relay_ui";

/**
 * @brief Control the relay hardware based on state
 * 
 * @param ui Pointer to the relay control UI object
 * @param state true = ON, false = OFF
 */
static void control_relay_hardware(relay_control_ui_t *ui, bool state)
{
    if (ui == NULL) {
        return;
    }
    
    // If hardware object is not configured, skip hardware control
    if (ui->hardware == NULL) {
        return;
    }
    
    // Control hardware through hardware abstraction layer
    relay_hardware_set_state(ui->hardware, state);
}

/**
 * @brief Animation callback for progress bar value update
 */
static void progress_bar_anim_cb(void *var, int32_t value)
{
    lv_obj_t *bar = (lv_obj_t *)var;
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
}

/**
 * @brief Format time as MM:SS string
 * 
 * @param seconds Total seconds
 * @param buffer Output buffer (at least 10 bytes)
 */
static void format_time_string(uint32_t seconds, char *buffer)
{
    uint32_t minutes = seconds / 60;
    uint32_t secs = seconds % 60;
    // Buffer is 10 bytes, format string "%02u:%02u" produces max 6 chars (5 + null)
    // Suppress format-truncation warning since values are bounded (min 0-30, sec 0-59)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(buffer, 10, "%02" PRIu32 ":%02" PRIu32, minutes, secs);
    #pragma GCC diagnostic pop
}

/**
 * @brief Update the timer display
 * 
 * @param ui Pointer to the relay control UI object
 */
static void update_timer_display(relay_control_ui_t *ui)
{
    if (ui == NULL || ui->timer_label == NULL) {
        return;
    }

    if (ui->state && ui->time_remaining > 0) {
        char time_str[10];
        format_time_string(ui->time_remaining, time_str);
        lv_label_set_text(ui->timer_label, time_str);
        lv_obj_clear_flag(ui->timer_label, LV_OBJ_FLAG_HIDDEN);
        
        // Update progress bar animation
        if (ui->progress_bar != NULL) {
            // Calculate progress percentage (0-100)
            uint32_t elapsed = RELAY_TIMER_DURATION_SECONDS - ui->time_remaining;
            int32_t progress = (int32_t)((elapsed * 100) / RELAY_TIMER_DURATION_SECONDS);
            // Ensure progress is between 0 and 100
            if (progress < 0) progress = 0;
            if (progress > 100) progress = 100;
            
            // Change color based on remaining time: green -> yellow -> red
            uint32_t remaining_percent = 100 - progress;
            lv_color_t bar_color;
            if (remaining_percent > 50) {
                // More than 50% remaining - green
                bar_color = lv_color_hex(0x00FF00);
            } else if (remaining_percent > 20) {
                // 20-50% remaining - yellow/orange
                bar_color = lv_color_hex(0xFFFF00);
            } else {
                // Less than 20% remaining - red
                bar_color = lv_color_hex(0xFF0000);
            }
            lv_obj_set_style_bg_color(ui->progress_bar, bar_color, LV_PART_INDICATOR);
            
            // Animate the progress bar value change (smooth transition)
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, ui->progress_bar);
            lv_anim_set_values(&a, lv_bar_get_value(ui->progress_bar), progress);
            lv_anim_set_time(&a, 1000);  // 1 second smooth animation
            lv_anim_set_exec_cb(&a, progress_bar_anim_cb);
            lv_anim_start(&a);
            
            lv_obj_clear_flag(ui->progress_bar, LV_OBJ_FLAG_HIDDEN);
        }
        
        ESP_LOGI(ui->tag, "Timer display updated: %s", time_str);
    } else {
        lv_label_set_text_static(ui->timer_label, "");
        lv_obj_add_flag(ui->timer_label, LV_OBJ_FLAG_HIDDEN);
        
        // Hide progress bar
        if (ui->progress_bar != NULL) {
            lv_obj_add_flag(ui->progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(ui->progress_bar, 0, LV_ANIM_OFF);
        }
    }
}

/**
 * @brief Update the button appearance based on relay state
 * 
 * @param ui Pointer to the relay control UI object
 */
static void update_button_appearance(relay_control_ui_t *ui)
{
    if (ui == NULL || ui->button == NULL || ui->label == NULL) {
        return;
    }

    // Format the label text with the relay name
    char label_text[32];
    const char *display_name = (ui->name != NULL && strlen(ui->name) > 0) ? ui->name : "RELAY";

    if (ui->state) {
        // ON state - Green color
        lv_obj_set_style_bg_color(ui->button, BUTTON_ON_COLOR, LV_PART_MAIN);
        snprintf(label_text, sizeof(label_text), "%s ON", display_name);
        lv_label_set_text(ui->label, label_text);
        ESP_LOGI(ui->tag, "Relay UI: ON (Green)");
    } else {
        // OFF state - Red color
        lv_obj_set_style_bg_color(ui->button, BUTTON_OFF_COLOR, LV_PART_MAIN);
        snprintf(label_text, sizeof(label_text), "%s OFF", display_name);
        lv_label_set_text(ui->label, label_text);
        ESP_LOGI(ui->tag, "Relay UI: OFF (Red)");
    }
    
    update_timer_display(ui);
}

/**
 * @brief Timer callback - counts down and sets flag for UI update
 * NOTE: This runs in esp_timer task context - DO NOT call LVGL functions here!
 */
static void timer_callback(void *arg)
{
    relay_control_ui_t *ui = (relay_control_ui_t *)arg;
    
    if (ui == NULL) {
        return;
    }

    if (ui->time_remaining > 0) {
        ui->time_remaining--;
        ui->update_needed = true;  // Signal that UI update is needed
        
        if (ui->time_remaining == 0) {
            // Timer expired - signal relay turn off
            ESP_LOGI(ui->tag, "Timer expired - will turn relay OFF");
            ui->state = false;
            ui->update_needed = true;
            // Control hardware immediately (safe to call from timer context)
            control_relay_hardware(ui, false);
            // Stop the timer
            if (ui->timer != NULL) {
                esp_timer_stop(ui->timer);
            }
        }
    }
}

/**
 * @brief LVGL timer callback - safely updates UI from LVGL context
 */
static void lvgl_timer_cb(lv_timer_t *timer)
{
    relay_control_ui_t *ui = (relay_control_ui_t *)lv_timer_get_user_data(timer);
    
    if (ui == NULL) {
        return;
    }
    
    // Check if update is needed (set from esp_timer callback)
    if (ui->update_needed) {
        ui->update_needed = false;  // Clear flag
        
        // Now safe to call LVGL functions since we're in LVGL timer context
        update_timer_display(ui);
        
        // If timer expired, also update button appearance
        if (ui->time_remaining == 0 && !ui->state) {
            update_button_appearance(ui);
        }
    }
}

/**
 * @brief Start the timer
 * 
 * @param ui Pointer to the relay control UI object
 */
static void start_timer(relay_control_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    // Stop any existing timer
    if (ui->timer != NULL) {
        esp_timer_stop(ui->timer);
        esp_timer_delete(ui->timer);
        ui->timer = NULL;
    }

    // Initialize time remaining
    ui->time_remaining = RELAY_TIMER_DURATION_SECONDS;

    // Reset progress bar to 0%
    if (ui->progress_bar != NULL) {
        lv_bar_set_value(ui->progress_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(ui->progress_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);  // Start green
    }

    // Create timer
    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .arg = ui,
        .name = "relay_timer"
    };

    esp_err_t ret = esp_timer_create(&timer_args, &ui->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(ui->tag, "Failed to create timer: %s", esp_err_to_name(ret));
        ui->timer = NULL;
        return;
    }

    // Start periodic timer (1 second intervals)
    ret = esp_timer_start_periodic(ui->timer, 1000000); // 1 second in microseconds
    if (ret != ESP_OK) {
        ESP_LOGE(ui->tag, "Failed to start timer: %s", esp_err_to_name(ret));
        esp_timer_delete(ui->timer);
        ui->timer = NULL;
        return;
    }

    ESP_LOGI(ui->tag, "Timer started: 30 minutes");
    update_timer_display(ui);
}

/**
 * @brief Stop the timer
 * 
 * @param ui Pointer to the relay control UI object
 */
static void stop_timer(relay_control_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    if (ui->timer != NULL) {
        esp_timer_stop(ui->timer);
        esp_timer_delete(ui->timer);
        ui->timer = NULL;
    }

    ui->time_remaining = 0;
    update_timer_display(ui);
}

/**
 * @brief Button click event callback
 */
static void relay_button_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    // Get the relay control UI object from user data
    relay_control_ui_t *ui = (relay_control_ui_t *)lv_event_get_user_data(e);
    
    if (ui == NULL) {
        return;
    }
    
    if (code == LV_EVENT_LONG_PRESSED) {
        // Long press: Turn ON without timer
        // Only turn ON if currently OFF
        if (!ui->state) {
            ui->state = true;
            
            // Control hardware based on new state
            control_relay_hardware(ui, ui->state);
            
            // DO NOT start timer for long press
            // Timer remains stopped/unchanged
            
            // Set flag to prevent CLICKED event from toggling after long press
            ui->long_press_active = true;
            
            update_button_appearance(ui);
            
            // Notify state change callback
            if (ui->state_change_cb != NULL) {
                ui->state_change_cb(ui, ui->state);
            }
            
            ESP_LOGI(ui->tag, "Relay button long-pressed, state: ON (no timer)");
        }
    } else if (code == LV_EVENT_CLICKED) {
        // Short click: Normal toggle behavior with timer
        // If long press just happened, ignore this CLICKED event to prevent toggling off
        if (ui->long_press_active) {
            ui->long_press_active = false;  // Reset flag and ignore this click
            ESP_LOGI(ui->tag, "Ignoring CLICKED event after long press");
            return;
        }
        
        bool old_state = ui->state;
        ui->state = !ui->state;
        
        // Control hardware based on new state
        control_relay_hardware(ui, ui->state);
        
        // Start timer when turning ON, stop when turning OFF
        if (ui->state && !old_state) {
            // Turning ON - start timer
            start_timer(ui);
        } else if (!ui->state && old_state) {
            // Turning OFF - stop timer
            stop_timer(ui);
        }
        
        update_button_appearance(ui);
        
        // Notify state change callback
        if (ui->state_change_cb != NULL) {
            ui->state_change_cb(ui, ui->state);
        }
        
        ESP_LOGI(ui->tag, "Relay button clicked, state: %s", ui->state ? "ON" : "OFF");
    }
}

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
relay_control_ui_t *relay_control_ui_create(lv_obj_t *parent, const char *tag, const char *name, lv_align_t align, int16_t x_offset, int16_t y_offset, relay_hardware_t *hardware)
{
    if (parent == NULL) {
        ESP_LOGE(DEFAULT_TAG, "Cannot create relay control UI: parent is NULL");
        return NULL;
    }

    // Allocate memory for the object
    relay_control_ui_t *ui = (relay_control_ui_t *)malloc(sizeof(relay_control_ui_t));
    if (ui == NULL) {
        ESP_LOGE(DEFAULT_TAG, "Failed to allocate memory for relay control UI");
        return NULL;
    }

    // Initialize the struct
    memset(ui, 0, sizeof(relay_control_ui_t));
    ui->tag = (tag != NULL) ? tag : DEFAULT_TAG;
    ui->name = (name != NULL) ? name : "RELAY";  // Default name if not provided
    ui->state = false;  // Start with relay OFF
    ui->hardware = hardware;  // Store hardware object pointer (can be NULL)
    
    // Sync UI state with hardware state if hardware is available
    if (ui->hardware != NULL) {
        ui->state = relay_hardware_get_state(ui->hardware);
    }

    // Create button
    ui->button = lv_button_create(parent);
    if (ui->button == NULL) {
        ESP_LOGE(ui->tag, "Failed to create button");
        free(ui);
        return NULL;
    }
    
    // Set button size
    lv_obj_set_size(ui->button, BUTTON_WIDTH_PX, BUTTON_HEIGHT_PX);
    
    // Position the button according to parameters
    lv_obj_align(ui->button, align, x_offset, y_offset);
    
    // Create label inside button
    ui->label = lv_label_create(ui->button);
    if (ui->label == NULL) {
        ESP_LOGE(ui->tag, "Failed to create label");
        lv_obj_del(ui->button);
        free(ui);
        return NULL;
    }
    lv_obj_center(ui->label);
    
    // Create timer label above the button
    ui->timer_label = lv_label_create(parent);
    if (ui->timer_label == NULL) {
        ESP_LOGE(ui->tag, "Failed to create timer label");
        lv_obj_del(ui->button);
        free(ui);
        return NULL;
    }
    
    // Position timer label above button (relative to button)
    lv_obj_align_to(ui->timer_label, ui->button, LV_ALIGN_OUT_TOP_MID, TIMER_LABEL_X_OFFSET_PX, TIMER_LABEL_Y_OFFSET_PX);
    lv_label_set_text_static(ui->timer_label, "");
    
    // Style the timer label - make it prominent
    lv_obj_set_style_text_align(ui->timer_label, TIMER_LABEL_TEXT_ALIGN, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->timer_label, TIMER_LABEL_TEXT_COLOR, LV_PART_MAIN);
    // Make sure it's not hidden initially so it can be shown when needed
    // Initially hide timer label (will show when relay is ON and timer starts)
    lv_obj_add_flag(ui->timer_label, LV_OBJ_FLAG_HIDDEN);
    
    // Create progress bar for animated countdown visualization
    ui->progress_bar = lv_bar_create(parent);
    if (ui->progress_bar == NULL) {
        ESP_LOGE(ui->tag, "Failed to create progress bar");
        lv_obj_del(ui->timer_label);
        lv_obj_del(ui->button);
        free(ui);
        return NULL;
    }
    
    // Position progress bar above button, between button and timer label
    lv_obj_align_to(ui->progress_bar, ui->button, LV_ALIGN_OUT_BOTTOM_MID, PROGRESS_BAR_X_OFFSET_PX, PROGRESS_BAR_Y_OFFSET_PX);
    lv_obj_set_size(ui->progress_bar, PROGRESS_BAR_WIDTH_PX, PROGRESS_BAR_HEIGHT_PX);
    
    // Style the progress bar
    lv_bar_set_value(ui->progress_bar, 0, LV_ANIM_OFF);
    lv_bar_set_range(ui->progress_bar, 0, 100);
    // Set progress bar color based on remaining time (green -> yellow -> red)
    lv_obj_set_style_bg_color(ui->progress_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);  // Green
    
    // Initially hide progress bar
    lv_obj_add_flag(ui->progress_bar, LV_OBJ_FLAG_HIDDEN);
    
    // Initialize timer fields and callbacks
    ui->timer = NULL;
    ui->time_remaining = 0;
    ui->update_needed = false;
    ui->long_press_active = false;  // Initialize long press flag
    ui->state_change_cb = NULL;
    ui->state_change_cb_arg = NULL;
    
    // Create LVGL timer for safe UI updates (runs in LVGL context)
    // This timer will periodically check update_needed flag and update UI safely
    ui->lvgl_timer = lv_timer_create(lvgl_timer_cb, 100, ui);  // Check every 100ms
    if (ui->lvgl_timer == NULL) {
        ESP_LOGE(ui->tag, "Failed to create LVGL timer");
        if (ui->progress_bar != NULL) {
            lv_obj_del(ui->progress_bar);
        }
        lv_obj_del(ui->timer_label);
        lv_obj_del(ui->button);
        free(ui);
        return NULL;
    }
    lv_timer_set_repeat_count(ui->lvgl_timer, -1);  // Repeat indefinitely
    
    // Set initial appearance
    update_button_appearance(ui);
    
    // Add click event callback with user data pointing to our object
    lv_obj_add_event_cb(ui->button, relay_button_cb, LV_EVENT_CLICKED, ui);
    
    // Add long press event callback for turning on without timer
    lv_obj_add_event_cb(ui->button, relay_button_cb, LV_EVENT_LONG_PRESSED, ui);
    
    // Make button style more prominent
    lv_obj_set_style_radius(ui->button, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui->button, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(ui->button, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(ui->button, LV_OPA_50, LV_PART_MAIN);
    
    ESP_LOGI(ui->tag, "Relay control UI object created");
    
    return ui;
}

/**
 * @brief Delete/destroy a relay control UI object
 * 
 * @param ui Pointer to the relay control UI object to destroy
 */
void relay_control_ui_delete(relay_control_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    // Stop and delete timers if they exist
    stop_timer(ui);
    
    if (ui->lvgl_timer != NULL) {
        lv_timer_del(ui->lvgl_timer);
        ui->lvgl_timer = NULL;
    }

    // Delete LVGL objects if they exist
    if (ui->progress_bar != NULL) {
        lv_obj_del(ui->progress_bar);
        ui->progress_bar = NULL;
    }
    
    if (ui->timer_label != NULL) {
        lv_obj_del(ui->timer_label);
        ui->timer_label = NULL;
    }
    
    if (ui->button != NULL) {
        lv_obj_del(ui->button);
        ui->button = NULL;
        ui->label = NULL;
    }

    // Free the object memory
    free(ui);
}

/**
 * @brief Get current relay state
 * 
 * @param ui Pointer to the relay control UI object
 * @return true if relay is ON, false if OFF
 */
bool relay_control_ui_get_state(const relay_control_ui_t *ui)
{
    if (ui == NULL) {
        return false;
    }
    return ui->state;
}

/**
 * @brief Set relay state programmatically
 * 
 * @param ui Pointer to the relay control UI object
 * @param state true for ON, false for OFF
 */
void relay_control_ui_set_state(relay_control_ui_t *ui, bool state)
{
    if (ui == NULL) {
        return;
    }
    
    // Validate object structure - button pointer should be valid for initialized objects
    if (ui->button == NULL) {
        ESP_LOGW(DEFAULT_TAG, "Attempted to set state on invalid relay UI object");
        return;
    }
    
    // Ensure tag is valid (will be used by control_relay_gpio for logging)
    // If tag is NULL, set it to default to prevent crash
    if (ui->tag == NULL) {
        ui->tag = DEFAULT_TAG;
    }
    
    bool old_state = ui->state;
    ui->state = state;
    
    // Control hardware based on new state
    control_relay_hardware(ui, ui->state);
    
    // Start timer when turning ON, stop when turning OFF
    if (ui->state && !old_state) {
        // Turning ON - start timer
        start_timer(ui);
    } else if (!ui->state && old_state) {
        // Turning OFF - stop timer
        stop_timer(ui);
    }
    
    update_button_appearance(ui);
    
    // Notify state change callback (for master button updates)
    if (ui->state_change_cb != NULL) {
        ui->state_change_cb(ui, ui->state);
    }
}

/**
 * @brief Toggle relay state programmatically
 * 
 * @param ui Pointer to the relay control UI object
 */
void relay_control_ui_toggle(relay_control_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    
    bool old_state = ui->state;
    ui->state = !ui->state;
    
    // Start timer when turning ON, stop when turning OFF
    if (ui->state && !old_state) {
        // Turning ON - start timer
        start_timer(ui);
    } else if (!ui->state && old_state) {
        // Turning OFF - stop timer
        stop_timer(ui);
    }
    
    update_button_appearance(ui);
    
    // Notify state change callback (for master button updates)
    if (ui->state_change_cb != NULL) {
        ui->state_change_cb(ui, ui->state);
    }
}

/**
 * @brief Set state change callback
 * 
 * @param ui Pointer to the relay control UI object
 * @param cb Callback function (can be NULL)
 * @param arg User data for callback
 */
void relay_control_ui_set_state_change_callback(relay_control_ui_t *ui, relay_state_change_cb_t cb, void *arg)
{
    if (ui == NULL) {
        return;
    }
    ui->state_change_cb = cb;
    ui->state_change_cb_arg = arg;
}

/**
 * @brief Get the button object (for advanced customization)
 * 
 * @param ui Pointer to the relay control UI object
 * @return lv_obj_t* Pointer to the button object
 */
lv_obj_t *relay_control_ui_get_button(relay_control_ui_t *ui)
{
    if (ui == NULL) {
        return NULL;
    }
    return ui->button;
}

