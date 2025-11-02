/*
 * Master Button UI Component
 * 
 * Provides a master button that can control multiple relay buttons.
 * The master button turns green when any controlled relay is ON,
 * and turns red when all controlled relays are OFF.
 * When clicked, it turns OFF all active relays.
 */

#include "master_button_ui.h"
#include "relay_control_ui.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl.h"
#include "esp_log.h"

static const char *DEFAULT_TAG = "master_btn";

/**
 * @brief Button click event callback for master button
 */
static void master_button_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    ESP_LOGI(DEFAULT_TAG, "Master button clicked");
    if (code != LV_EVENT_CLICKED) {
        return;  // Only handle click events
    }
    
    // Get the master button UI object from user data
    master_button_ui_t *master = (master_button_ui_t *)lv_event_get_user_data(e);

    if (master == NULL) {
        ESP_LOGE(DEFAULT_TAG, "Master button callback received NULL master object");
        return;
    }

    // Validate master object structure
    if (master->button == NULL) {
        const char *tag = (master->tag != NULL) ? master->tag : DEFAULT_TAG;
        ESP_LOGE(tag, "Master button object has NULL button pointer");
        return;
    }
    
    // Master button clicked - turn OFF all controlled relays that are ON
    bool any_changed = false;
    // Validate controlled_relays array before iterating
    if (master->controlled_relays == NULL || master->num_controlled_relays == 0) {
        const char *tag = (master->tag != NULL) ? master->tag : DEFAULT_TAG;
        ESP_LOGW(tag, "Master button: controlled_relays not configured");
    } else {
        ESP_LOGI(DEFAULT_TAG, "Controlled relays: %d", master->num_controlled_relays);
        for (uint8_t i = 0; i < master->num_controlled_relays; i++) {
            relay_control_ui_t *relay = master->controlled_relays[i];
            // Step-by-step validation to avoid crashes
            if (relay == NULL) {
                continue;  // Skip NULL entries
            }
            
            // Check if relay object is valid by checking button pointer
            if (relay->button == NULL) {
                ESP_LOGI(DEFAULT_TAG, "Relay button: %p", relay->button);
                const char *tag = (master->tag != NULL) ? master->tag : DEFAULT_TAG;
                ESP_LOGW(tag, "Master button: Skipping invalid relay object at index %d", i);
                continue;  // Invalid relay object - skip it
            }

            // Use safe API to get state instead of direct access
            if (relay_control_ui_get_state(relay)) {
                // Turn off the relay using safe API
                relay_control_ui_set_state(relay, false);
                any_changed = true;
            }
        }
    }
        // Update master button appearance after turning off relays
        master_button_ui_update_appearance(master);

}

/**
 * @brief Create a new master button UI object
 * 
 * @param parent Parent object (usually the screen)
 * @param tag Log tag for this instance (can be NULL for default)
 * @param name Display name for the master button (e.g., "Master")
 * @param align Alignment type (e.g., LV_ALIGN_CENTER, LV_ALIGN_TOP_LEFT, etc.)
 * @param x_offset X offset from alignment point
 * @param y_offset Y offset from alignment point
 * @return master_button_ui_t* Pointer to the created master button UI object, or NULL on failure
 */
master_button_ui_t *master_button_ui_create(lv_obj_t *parent, const char *tag, const char *name, lv_align_t align, int16_t x_offset, int16_t y_offset)
{
    if (parent == NULL) {
        ESP_LOGE(DEFAULT_TAG, "Cannot create master button UI: parent is NULL");
        return NULL;
    }

    // Allocate memory for the object
    master_button_ui_t *master = (master_button_ui_t *)malloc(sizeof(master_button_ui_t));
    if (master == NULL) {
        ESP_LOGE(DEFAULT_TAG, "Failed to allocate memory for master button UI");
        return NULL;
    }

    // Initialize the struct
    memset(master, 0, sizeof(master_button_ui_t));
    master->tag = (tag != NULL) ? tag : DEFAULT_TAG;
    master->name = (name != NULL) ? name : "Master";  // Default name if not provided
    master->controlled_relays = NULL;
    master->num_controlled_relays = 0;

    // Create button
    master->button = lv_button_create(parent);
    if (master->button == NULL) {
        ESP_LOGE(master->tag, "Failed to create master button");
        free(master);
        return NULL;
    }
    
    // Set button size
    lv_obj_set_size(master->button, MASTER_BUTTON_WIDTH_PX, MASTER_BUTTON_HEIGHT_PX);
    
    // Position the button according to parameters
    lv_obj_align(master->button, align, x_offset, y_offset);
    
    // Create label inside button
    master->label = lv_label_create(master->button);
    if (master->label == NULL) {
        ESP_LOGE(master->tag, "Failed to create master button label");
        lv_obj_del(master->button);
        free(master);
        return NULL;
    }
    lv_obj_center(master->label);
    
    // Set initial appearance (OFF state - red)
    lv_obj_set_style_bg_color(master->button, MASTER_BUTTON_OFF_COLOR, LV_PART_MAIN);
    lv_label_set_text_static(master->label, "Master OFF");
    
    // Make button style more prominent
    lv_obj_set_style_radius(master->button, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(master->button, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(master->button, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(master->button, LV_OPA_50, LV_PART_MAIN);
    
    // Add click event callback with user data pointing to our object
    lv_obj_add_event_cb(master->button, master_button_cb, LV_EVENT_CLICKED, master);
    
    ESP_LOGI(master->tag, "Master button UI object created");
    
    return master;
}

/**
 * @brief Delete/destroy a master button UI object
 * 
 * @param master Pointer to the master button UI object to destroy
 */
void master_button_ui_delete(master_button_ui_t *master)
{
    if (master == NULL) {
        return;
    }

    // Free controlled_relays array if it exists
    if (master->controlled_relays != NULL) {
        free(master->controlled_relays);
        master->controlled_relays = NULL;
    }

    // Delete LVGL objects if they exist
    if (master->button != NULL) {
        lv_obj_del(master->button);
        master->button = NULL;
    }

    // Free the object
    free(master);
}

/**
 * @brief Set controlled relays for master button
 * 
 * @param master Master button UI object
 * @param relays Array of relay UI objects to control
 * @param num_relays Number of relays in the array
 */
void master_button_ui_set_controlled_relays(master_button_ui_t *master, relay_control_ui_t **relays, uint8_t num_relays)
{
    if (master == NULL || relays == NULL || num_relays == 0) {
        const char *tag = (master && master->tag != NULL) ? master->tag : DEFAULT_TAG;
        ESP_LOGW(tag, "Invalid arguments for set_controlled_relays");
        return;
    }
    
    // Free any existing controlled_relays array if it exists
    if (master->controlled_relays != NULL) {
        free(master->controlled_relays);
        master->controlled_relays = NULL;
    }
    
    // Allocate memory for the array and copy the pointers
    master->controlled_relays = (relay_control_ui_t **)malloc(sizeof(relay_control_ui_t *) * num_relays);
    if (master->controlled_relays == NULL) {
        const char *tag = (master->tag != NULL) ? master->tag : DEFAULT_TAG;
        ESP_LOGE(tag, "Failed to allocate memory for controlled_relays array");
        master->num_controlled_relays = 0;
        return;
    }
    
    // Copy the relay pointers
    for (uint8_t i = 0; i < num_relays; i++) {
        master->controlled_relays[i] = relays[i];
    }
    
    master->num_controlled_relays = num_relays;
    
    const char *tag = (master->tag != NULL) ? master->tag : DEFAULT_TAG;
    ESP_LOGI(tag, "Master button configured to control %u relays", num_relays);
}

/**
 * @brief Update master button appearance based on controlled relays
 * 
 * @param master Master button UI object
 */
void master_button_ui_update_appearance(master_button_ui_t *master)
{
    if (master == NULL || master->button == NULL || master->label == NULL) {
        return;
    }
    
    // Check if any controlled relay is ON
    bool any_on = false;
    
    if (master->controlled_relays != NULL && master->num_controlled_relays > 0) {
        for (uint8_t i = 0; i < master->num_controlled_relays; i++) {
            relay_control_ui_t *relay = master->controlled_relays[i];
            // Validate relay before accessing state - use safe API
            if (relay != NULL && relay->button != NULL && relay_control_ui_get_state(relay)) {
                any_on = true;
                break;
            }
        }
    }
    
    // Update master button appearance
    if (any_on) {
        // At least one relay is ON - show green
        lv_obj_set_style_bg_color(master->button, MASTER_BUTTON_ON_COLOR, LV_PART_MAIN);
        lv_label_set_text_static(master->label, "Master ON");
    } else {
        // All relays are OFF - show red
        lv_obj_set_style_bg_color(master->button, MASTER_BUTTON_OFF_COLOR, LV_PART_MAIN);
        lv_label_set_text_static(master->label, "Master OFF");
    }
}

/**
 * @brief Get the button object (for advanced customization)
 * 
 * @param master Pointer to the master button UI object
 * @return lv_obj_t* Pointer to the button object
 */
lv_obj_t *master_button_ui_get_button(master_button_ui_t *master)
{
    if (master == NULL) {
        return NULL;
    }
    return master->button;
}

