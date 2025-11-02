/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"
#include "misc/lv_area.h"
#include "relay_control_ui.h"
#include "master_button_ui.h"
#include "relay_hardware.h"
#include "driver/gpio.h"

// Global pointers to relay hardware objects
static relay_hardware_t *relay_1_hw = NULL;
static relay_hardware_t *relay_2_hw = NULL;
static relay_hardware_t *relay_3_hw = NULL;
static relay_hardware_t *relay_4_hw = NULL;
static relay_hardware_t *relay_5_hw = NULL;

// Global pointer to relay control UI object (can be used from other parts of the code)
static master_button_ui_t *master_ui_obj = NULL;
static relay_control_ui_t *relay_1_ui_obj = NULL;
static relay_control_ui_t *relay_2_ui_obj = NULL;
static relay_control_ui_t *relay_3_ui_obj = NULL;
static relay_control_ui_t *relay_4_ui_obj = NULL;
static relay_control_ui_t *relay_5_ui_obj = NULL;

/**
 * @brief Callback function called when any relay changes state
 * Updates the master button appearance based on controlled relays
 */
static void relay_state_changed_cb(relay_control_ui_t *relay, bool new_state)
{
    (void)relay;  // Unused parameter
    (void)new_state;  // Unused parameter
    
    // Update master button appearance when any relay changes
    if (master_ui_obj != NULL) {
        master_button_ui_update_appearance(master_ui_obj);
    }
}

void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    // Create the master button first
    master_ui_obj = master_button_ui_create(scr, "master_ui", "Master", LV_ALIGN_TOP_LEFT, 20, 20);
    if (master_ui_obj == NULL) {
        return;
    }
    
    // Create relay hardware objects first
    // Note: Adjust GPIO pin numbers according to your hardware configuration
    relay_1_hw = relay_hardware_create(GPIO_NUM_35, "relay_1_hw");
    relay_2_hw = relay_hardware_create(GPIO_NUM_36, "relay_2_hw");
    relay_3_hw = relay_hardware_create(GPIO_NUM_37, "relay_3_hw");
    relay_4_hw = relay_hardware_create(GPIO_NUM_38, "relay_4_hw");
    relay_5_hw = relay_hardware_create(GPIO_NUM_39, "relay_5_hw");
    
    // Create the relay control UI objects with hardware references
    relay_1_ui_obj = relay_control_ui_create(scr, "relay_1_ui", "Relay 1", LV_ALIGN_TOP_RIGHT, -20, 20, relay_1_hw);
    relay_2_ui_obj = relay_control_ui_create(scr, "relay_2_ui", "Relay 2", LV_ALIGN_LEFT_MID, 20, 0, relay_2_hw);
    relay_3_ui_obj = relay_control_ui_create(scr, "relay_3_ui", "Relay 3", LV_ALIGN_RIGHT_MID, -20, 0, relay_3_hw);    
    relay_4_ui_obj = relay_control_ui_create(scr, "relay_4_ui", "Relay 4", LV_ALIGN_BOTTOM_LEFT, 20, -20, relay_4_hw);
    relay_5_ui_obj = relay_control_ui_create(scr, "relay_5_ui", "Relay 5", LV_ALIGN_BOTTOM_RIGHT, -20, -20, relay_5_hw);  

    if (relay_1_ui_obj == NULL || relay_2_ui_obj == NULL || relay_3_ui_obj == NULL || relay_4_ui_obj == NULL || relay_5_ui_obj == NULL) {
        // Error creating UI object - this will be logged by the create function
        return;
    }
    
    // Set up controlled relays array for master button
    relay_control_ui_t *controlled_relays[5];
    controlled_relays[0] = relay_1_ui_obj;
    controlled_relays[1] = relay_2_ui_obj;
    controlled_relays[2] = relay_3_ui_obj;
    controlled_relays[3] = relay_4_ui_obj;
    controlled_relays[4] = relay_5_ui_obj;
    
    master_button_ui_set_controlled_relays(master_ui_obj, controlled_relays, 5);
    
    // Set state change callbacks for all relays to update master button
    relay_control_ui_set_state_change_callback(relay_1_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_2_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_3_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_4_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_5_ui_obj, relay_state_changed_cb, NULL);
    
    // Initialize master button appearance
    master_button_ui_update_appearance(master_ui_obj);
}
