/*
 * SmartSocket LVGL UI - composition root
 *
 * Creates the relay hardware objects, the per-relay control tiles,
 * the (optional) master button and the IP status label.
 */

#include "lvgl_demo_ui.h"
#include "relay_control_ui.h"
#include "master_button_ui.h"
#include "relay_hardware.h"
#include "driver/gpio.h"
#include "hal/adc_types.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief Static configuration for one relay: pins, ADC channel and screen placement
 *
 * Adjust pin numbers and ADC channels here to match your board wiring.
 */
typedef struct {
    gpio_num_t relay_pin;       // Relay output GPIO
    gpio_num_t led_pin;         // Indicator LED GPIO
    adc_unit_t adc_unit;        // ADC unit for ACS712 current sensing
    adc_channel_t adc_channel;  // ADC channel for ACS712 current sensing
    lv_align_t align;           // Screen alignment of the tile
    int16_t x_offset;           // X offset from alignment point
    int16_t y_offset;           // Y offset from alignment point
    const char *hw_tag;         // Log tag for the hardware object
    const char *ui_tag;         // Log tag for the UI object
    const char *name;           // Display name shown on the button
} relay_config_t;

static const relay_config_t s_relay_configs[SMARTSOCKET_RELAY_COUNT] = {
    { GPIO_NUM_35, GPIO_NUM_48, ADC_UNIT_1, ADC_CHANNEL_3, LV_ALIGN_TOP_LEFT,      20,  20, "relay_1_hw", "relay_1_ui", "Relay 1" },
    { GPIO_NUM_36, GPIO_NUM_21, ADC_UNIT_1, ADC_CHANNEL_4, LV_ALIGN_TOP_RIGHT,    -20,  20, "relay_2_hw", "relay_2_ui", "Relay 2" },
    { GPIO_NUM_37, GPIO_NUM_2,  ADC_UNIT_1, ADC_CHANNEL_5, LV_ALIGN_LEFT_MID,      20,   0, "relay_3_hw", "relay_3_ui", "Relay 3" },
    { GPIO_NUM_38, GPIO_NUM_14, ADC_UNIT_1, ADC_CHANNEL_6, LV_ALIGN_RIGHT_MID,    -20,   0, "relay_4_hw", "relay_4_ui", "Relay 4" },
    { GPIO_NUM_39, GPIO_NUM_13, ADC_UNIT_2, ADC_CHANNEL_0, LV_ALIGN_BOTTOM_LEFT,   20, -20, "relay_5_hw", "relay_5_ui", "Relay 5" },
    { GPIO_NUM_40, GPIO_NUM_47, ADC_UNIT_2, ADC_CHANNEL_1, LV_ALIGN_BOTTOM_RIGHT, -20, -20, "relay_6_hw", "relay_6_ui", "Relay 6" },
};

static relay_hardware_t *s_relay_hw[SMARTSOCKET_RELAY_COUNT];
static relay_control_ui_t *s_relay_ui[SMARTSOCKET_RELAY_COUNT];
static master_button_ui_t *s_master_ui = NULL;
static lv_obj_t *s_ip_label = NULL;

// UI init progress, readable over HTTP (/api/debug) since UART may not be attached
static char s_init_status[160] = "not started";

const char *example_lvgl_get_init_status(void)
{
    return s_init_status;
}

/**
 * @brief Callback function called when any relay changes state
 * Updates the master button appearance based on controlled relays
 */
static void relay_state_changed_cb(relay_control_ui_t *relay, bool new_state)
{
    (void)relay;
    (void)new_state;

    if (s_master_ui != NULL) {
        master_button_ui_update_appearance(s_master_ui);
    }
}

void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    // Master button is currently disabled; uncomment to enable it.
    // s_master_ui = master_button_ui_create(scr, "master_ui", "Master", LV_ALIGN_TOP_LEFT, 20, 20);

    for (int i = 0; i < SMARTSOCKET_RELAY_COUNT; i++) {
        const relay_config_t *cfg = &s_relay_configs[i];

        snprintf(s_init_status, sizeof(s_init_status), "creating relay %d hardware", i + 1);
        s_relay_hw[i] = relay_hardware_create(cfg->relay_pin, cfg->led_pin,
                                              cfg->adc_unit, cfg->adc_channel, cfg->hw_tag);

        snprintf(s_init_status, sizeof(s_init_status), "creating relay %d ui", i + 1);
        s_relay_ui[i] = relay_control_ui_create(scr, cfg->ui_tag, cfg->name,
                                                cfg->align, cfg->x_offset, cfg->y_offset,
                                                s_relay_hw[i]);
        if (s_relay_ui[i] == NULL) {
            // Record the failure but keep creating the remaining relays
            snprintf(s_init_status, sizeof(s_init_status), "relay %d ui create FAILED: %s",
                     i + 1, relay_control_ui_last_error());
            continue;
        }

        relay_control_ui_set_state_change_callback(s_relay_ui[i], relay_state_changed_cb, NULL);
    }

    if (s_master_ui != NULL) {
        master_button_ui_set_controlled_relays(s_master_ui, s_relay_ui, SMARTSOCKET_RELAY_COUNT);
        master_button_ui_update_appearance(s_master_ui);
    }

    // Create IP address label at the bottom of the screen
    s_ip_label = lv_label_create(scr);
    lv_label_set_text(s_ip_label, "IP: --");
    lv_obj_set_style_text_color(s_ip_label, lv_color_hex(0x808080), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ip_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(s_ip_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_align(s_ip_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    if (strncmp(s_init_status, "relay", 5) != 0) {
        snprintf(s_init_status, sizeof(s_init_status), "done");
    }
}

void example_lvgl_update_ip_address(const char *ip_str)
{
    if (s_ip_label == NULL) {
        return;
    }

    if (ip_str != NULL && strlen(ip_str) > 0) {
        char label_text[32];
        snprintf(label_text, sizeof(label_text), "IP: %s", ip_str);
        lv_label_set_text(s_ip_label, label_text);
        lv_obj_set_style_text_color(s_ip_label, lv_color_hex(0x00FF00), LV_PART_MAIN); // Green when connected
    } else {
        lv_label_set_text(s_ip_label, "IP: --");
        lv_obj_set_style_text_color(s_ip_label, lv_color_hex(0x808080), LV_PART_MAIN); // Gray when not connected
    }
}

relay_control_ui_t *example_lvgl_get_relay_ui(int index)
{
    if (index < 1 || index > SMARTSOCKET_RELAY_COUNT) {
        return NULL;
    }
    return s_relay_ui[index - 1];
}
