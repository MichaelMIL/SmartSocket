/*
 * Master Button UI Component Header
 * 
 * Provides a master button that can control multiple relay buttons.
 * The master button turns green when any controlled relay is ON,
 * and turns red when all controlled relays are OFF.
 * When clicked, it turns OFF all active relays.
 */

#ifndef MASTER_BUTTON_UI_H
#define MASTER_BUTTON_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

// Forward declaration
struct relay_control_ui_s;
typedef struct relay_control_ui_s relay_control_ui_t;

#ifdef __cplusplus
extern "C" {
#endif

#define MASTER_BUTTON_WIDTH_PX 100
#define MASTER_BUTTON_HEIGHT_PX 60
#define MASTER_BUTTON_OFF_COLOR lv_color_hex(0xC00000)
#define MASTER_BUTTON_ON_COLOR lv_color_hex(0x00C000)

/**
 * @brief Master Button UI object structure
 */
typedef struct {
    lv_obj_t *button;          // The button object
    lv_obj_t *label;           // The label inside the button
    const char *tag;            // Log tag for this instance
    const char *name;           // Display name (e.g., "Master")
    relay_control_ui_t **controlled_relays; // Array of pointers to controlled relays
    uint8_t num_controlled_relays; // Number of controlled relays
} master_button_ui_t;

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
master_button_ui_t *master_button_ui_create(lv_obj_t *parent, const char *tag, const char *name, lv_align_t align, int16_t x_offset, int16_t y_offset);

/**
 * @brief Delete/destroy a master button UI object
 * 
 * @param ui Pointer to the master button UI object to destroy
 */
void master_button_ui_delete(master_button_ui_t *ui);

/**
 * @brief Set controlled relays for master button
 * 
 * @param master Master button UI object
 * @param relays Array of relay UI objects to control
 * @param num_relays Number of relays in the array
 */
void master_button_ui_set_controlled_relays(master_button_ui_t *master, relay_control_ui_t **relays, uint8_t num_relays);

/**
 * @brief Update master button appearance based on controlled relays
 * 
 * @param master Master button UI object
 */
void master_button_ui_update_appearance(master_button_ui_t *master);

/**
 * @brief Get the button object (for advanced customization)
 * 
 * @param ui Pointer to the master button UI object
 * @return lv_obj_t* Pointer to the button object
 */
lv_obj_t *master_button_ui_get_button(master_button_ui_t *ui);

#ifdef __cplusplus
}
#endif

#endif // MASTER_BUTTON_UI_H

