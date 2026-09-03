/*
 * SmartSocket LVGL UI - public API
 *
 * Composition root for the relay control screen. Consumed by app_main
 * (screen creation, IP label updates) and the HTTP server (relay access).
 */

#ifndef LVGL_DEMO_UI_H
#define LVGL_DEMO_UI_H

#include "lvgl.h"
#include "relay_control_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SMARTSOCKET_RELAY_COUNT 6

/**
 * @brief Create the relay control screen (relay tiles, master button, IP label)
 *
 * Must be called from the LVGL context (or with the LVGL lock held).
 *
 * @param disp Display to build the UI on
 */
void example_lvgl_demo_ui(lv_display_t *disp);

/**
 * @brief Update the IP address label on the screen
 *
 * Must be called from the LVGL context (or with the LVGL lock held).
 *
 * @param ip_str IP address string, or NULL/empty to show "IP: --"
 */
void example_lvgl_update_ip_address(const char *ip_str);

/**
 * @brief Get relay UI object by index
 *
 * @param index Relay index (1..SMARTSOCKET_RELAY_COUNT)
 * @return Pointer to the relay UI object, or NULL if invalid index or not created yet
 */
relay_control_ui_t *example_lvgl_get_relay_ui(int index);

/**
 * @brief Get a short description of UI init progress (for diagnostics without UART)
 *
 * @return Static string, e.g. "not started", "creating relay 3 ui", "done"
 */
const char *example_lvgl_get_init_status(void);

#ifdef __cplusplus
}
#endif

#endif // LVGL_DEMO_UI_H
