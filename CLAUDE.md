# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SmartSocket is an ESP-IDF firmware for an ESP32-S3 smart power socket: up to 6 relays controlled from an LVGL touch UI on an SPI LCD (ILI9341 + XPT2046 touch per current sdkconfig) and from a web interface served by the device. Includes OTA firmware updates over HTTP.

## Build Commands

Requires ESP-IDF installed and exported (`. $IDF_PATH/export.sh`). There are no tests or linters — verification is build + flash on hardware.

```bash
idf.py set-target esp32s3          # once, per checkout
idf.py build
idf.py -p <PORT> flash monitor     # e.g. /dev/tty.usbserial-xxxx on macOS
idf.py menuconfig                  # LCD/touch controller selection lives under "Example Configuration"
```

- LCD controller (ILI9341/GC9A01) and touch (STMPE610/XPT2046, on/off) are Kconfig choices defined in `main/Kconfig.projbuild` and compiled in via `#if CONFIG_EXAMPLE_...` blocks.
- Custom partition table (`partitions.csv`): factory + ota_0 + ota_1 app slots plus a `spiffs` partition. `main/web/` is packed into the SPIFFS image at build time (`spiffs_create_partition_image` in `main/CMakeLists.txt`) and flashed with the project — changes to `web/index.html` require a rebuild + flash.
- After the first flash, firmware can be updated over the network: browse to `http://<device-ip>/update` and upload the new `.bin` (OTA A/B slots handle the rest), or `curl -F "firmware=@build/spi_lcd_touch.bin" http://<device-ip>/update`. The web page updates separately: `curl --data-binary @main/web/index.html http://<device-ip>/update-web`.
- **Versioning:** bump `PROJECT_VER` in the root `CMakeLists.txt` on every firmware release and `WEB_VERSION` in `main/web/index.html` on every page release, then verify the deployed versions via `GET /api/debug` (firmware) and the page header (web). `GET /api/debug` also reports UI init status and heap stats.

## Architecture

Everything is registered as part of the `main` component (`main/CMakeLists.txt` lists all sources); the `main/components/` folders are organizational, not separate IDF components.

**Startup flow** (`main/spi_lcd_touch_example_main.c`, `app_main`): NVS init → `wifi_ota_init()` (blocks until WiFi connects or fails; auto-starts the HTTP server on port 80 when `ota_url` is NULL) → SPI bus + LCD panel + LVGL 9.2 init → LVGL tick timer + LVGL task → `example_lvgl_demo_ui()` builds the screen → IP label updated. WiFi credentials come from Kconfig (`idf.py menuconfig` → Example Configuration → WiFi SSID / WiFi password; defaults are placeholders and must be set to connect).

**Layers:**

- `main/components/relay_control_ui/relay_hardware.[ch]` — hardware abstraction per relay: relay GPIO, indicator-LED GPIO, and ADC channel for ACS712 current sensing (`relay_hardware_read_current()` returns Amps). No LVGL dependency.
- `main/components/relay_control_ui/relay_control_ui.[ch]` — LVGL widget per relay (button + countdown label + progress bar + current readout). Owns a 30-minute auto-off timer (`RELAY_TIMER_DURATION_SECONDS`). Long-press behavior is tracked with `long_press_active` to suppress the trailing CLICKED event. Notifies state changes via `relay_state_change_cb_t`.
- `main/components/relay_control_ui/master_button_ui.[ch]` — all-relays master button. Currently its creation is commented out in `lvgl_demo_ui.c` (`master_ui_obj` stays NULL; APIs are NULL-guarded).
- `main/lvgl_demo_ui.c`/`.h` — composition root: the `s_relay_configs` table (all relay/LED/ADC pin assignments and tile placement live here), the 6 relay tiles, and the IP label. Its public API (`example_lvgl_demo_ui()`, `example_lvgl_get_relay_ui(index)`, `example_lvgl_update_ip_address()`) is declared in `lvgl_demo_ui.h`, included by `app_main` and the HTTP server. `SMARTSOCKET_RELAY_COUNT` (6) is defined there.
- `main/components/wifi_ota/wifi_ota.[ch]` — WiFi STA connect + OTA via `esp_https_ota`.
- `main/components/wifi_ota/http_server.[ch]` — mounts SPIFFS at `/spiffs`, serves `index.html`, and registers: `GET /` (control page), `GET/POST /update` (OTA firmware upload), `GET/POST /api/relay/1`..`/api/relay/6` (JSON `{"success":true,"id":N,"state":bool}`; POST body carries `"state":true/false`).

**LVGL threading rule:** LVGL is not thread-safe. The main file guards LVGL calls with `lvgl_api_lock` (`_lock_acquire/_lock_release`) around the LVGL task and any UI work in `app_main`. Code running outside the LVGL task (esp_timer callbacks, HTTP handlers) must NOT touch LVGL objects directly — instead it sets the widget's `volatile bool update_needed` / `state_update_needed` flags, which a 100 ms `lv_timer` (`lvgl_timer_cb` in `relay_control_ui.c`) polls and applies safely. Follow this pattern for any new cross-context UI updates.

**Hardware pin map:** LCD/touch SPI pins are `#define`s at the top of `spi_lcd_touch_example_main.c`; relay outputs (GPIO 35–40), LED pins, and ADC channels are set in the `s_relay_configs` table in `lvgl_demo_ui.c`.
