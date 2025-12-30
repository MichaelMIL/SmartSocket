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
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#include <string.h>
#include <stdio.h>

// Global pointers to relay hardware objects
static relay_hardware_t *relay_1_hw = NULL;
static relay_hardware_t *relay_2_hw = NULL;
static relay_hardware_t *relay_3_hw = NULL;
static relay_hardware_t *relay_4_hw = NULL;
static relay_hardware_t *relay_5_hw = NULL;
static relay_hardware_t *relay_6_hw = NULL;

// Global pointer to relay control UI object (can be used from other parts of the code)
static master_button_ui_t *master_ui_obj = NULL;
static relay_control_ui_t *relay_1_ui_obj = NULL;
static relay_control_ui_t *relay_2_ui_obj = NULL;
static relay_control_ui_t *relay_3_ui_obj = NULL;
static relay_control_ui_t *relay_4_ui_obj = NULL;
static relay_control_ui_t *relay_5_ui_obj = NULL;
static relay_control_ui_t *relay_6_ui_obj = NULL;

// Global pointer to IP address label
static lv_obj_t *ip_label = NULL;

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
    // master_ui_obj = master_button_ui_create(scr, "master_ui", "Master", LV_ALIGN_TOP_LEFT, 20, 20);
    // if (master_ui_obj == NULL) {
    //     return;
    // }
    
    // Create relay hardware objects first
    // Note: Adjust GPIO pin numbers and ADC channels according to your hardware configuration
    // ADC1: channels 3-6 (4 relays)
    // ADC2: channels 0-1 (2 relays)
    // LED pins: 47, 48, 21, 13, 14, 2 (one for each relay)
    relay_1_hw = relay_hardware_create(GPIO_NUM_35, GPIO_NUM_48, ADC_UNIT_1, ADC_CHANNEL_3, "relay_1_hw");
    relay_2_hw = relay_hardware_create(GPIO_NUM_36, GPIO_NUM_21, ADC_UNIT_1, ADC_CHANNEL_4, "relay_2_hw");
    relay_3_hw = relay_hardware_create(GPIO_NUM_37, GPIO_NUM_2, ADC_UNIT_1, ADC_CHANNEL_5, "relay_3_hw");
    relay_4_hw = relay_hardware_create(GPIO_NUM_38, GPIO_NUM_14, ADC_UNIT_1, ADC_CHANNEL_6, "relay_4_hw");
    relay_5_hw = relay_hardware_create(GPIO_NUM_39, GPIO_NUM_13, ADC_UNIT_2, ADC_CHANNEL_0, "relay_5_hw");
    relay_6_hw = relay_hardware_create(GPIO_NUM_40, GPIO_NUM_47, ADC_UNIT_2, ADC_CHANNEL_1, "relay_6_hw");
    
    // Create the relay control UI objects with hardware references
    relay_1_ui_obj = relay_control_ui_create(scr, "relay_1_ui", "Relay 1", LV_ALIGN_TOP_LEFT, 20, 20, relay_1_hw);
    relay_2_ui_obj = relay_control_ui_create(scr, "relay_2_ui", "Relay 2", LV_ALIGN_TOP_RIGHT, -20, 20, relay_2_hw);
    relay_3_ui_obj = relay_control_ui_create(scr, "relay_3_ui", "Relay 3", LV_ALIGN_LEFT_MID, 20, 0, relay_3_hw);
    relay_4_ui_obj = relay_control_ui_create(scr, "relay_4_ui", "Relay 4", LV_ALIGN_RIGHT_MID, -20, 0, relay_4_hw);    
    relay_5_ui_obj = relay_control_ui_create(scr, "relay_5_ui", "Relay 5", LV_ALIGN_BOTTOM_LEFT, 20, -20, relay_5_hw);
    relay_6_ui_obj = relay_control_ui_create(scr, "relay_6_ui", "Relay 6", LV_ALIGN_BOTTOM_RIGHT, -20, -20, relay_6_hw);  

    if (relay_1_ui_obj == NULL || relay_2_ui_obj == NULL || relay_3_ui_obj == NULL || relay_4_ui_obj == NULL || relay_5_ui_obj == NULL) {
        // Error creating UI object - this will be logged by the create function
        return;
    }
    
    // Set up controlled relays array for master button
    relay_control_ui_t *controlled_relays[6];
    controlled_relays[0] = relay_1_ui_obj;
    controlled_relays[1] = relay_2_ui_obj;
    controlled_relays[2] = relay_3_ui_obj;
    controlled_relays[3] = relay_4_ui_obj;
    controlled_relays[4] = relay_5_ui_obj;
    controlled_relays[5] = relay_6_ui_obj;
    master_button_ui_set_controlled_relays(master_ui_obj, controlled_relays, 6);
    
    // Set state change callbacks for all relays to update master button
    relay_control_ui_set_state_change_callback(relay_1_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_2_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_3_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_4_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_5_ui_obj, relay_state_changed_cb, NULL);
    relay_control_ui_set_state_change_callback(relay_6_ui_obj, relay_state_changed_cb, NULL);
    // Initialize master button appearance
    master_button_ui_update_appearance(master_ui_obj);
    
    // Create IP address label at the bottom of the screen
    ip_label = lv_label_create(scr);
    lv_label_set_text(ip_label, "IP: --");
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0x808080), LV_PART_MAIN); // Gray color
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_14, LV_PART_MAIN); // Small font
    lv_obj_align(ip_label, LV_ALIGN_BOTTOM_MID, 0, -5); // Center at bottom with 5px margin
    lv_obj_set_style_text_align(ip_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

/**
 * @brief Update the IP address label on the screen
 * 
 * @param ip_str IP address string (can be NULL to show "IP: --")
 */
void example_lvgl_update_ip_address(const char *ip_str)
{
    if (ip_label == NULL) {
        return;
    }
    
    if (ip_str != NULL && strlen(ip_str) > 0) {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "IP: %s", ip_str);
        lv_label_set_text(ip_label, label_text);
        lv_obj_set_style_text_color(ip_label, lv_color_hex(0x00FF00), LV_PART_MAIN); // Green when connected
    } else {
        lv_label_set_text(ip_label, "IP: --");
        lv_obj_set_style_text_color(ip_label, lv_color_hex(0x808080), LV_PART_MAIN); // Gray when not connected
    }
}
