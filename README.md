## SmartSocket – LVGL Relay Control UI

SmartSocket is an ESP-IDF project that uses LVGL to provide a graphical UI for controlling up to **six relays** and monitoring their state on an SPI LCD. The UI also shows the current IP address of the device so you can easily confirm network connectivity.

The main LVGL UI is implemented in `main/lvgl_demo_ui.c`, which wires together:
- **Relay hardware** (`relay_hardware_t` instances, one per relay)
- **Per‑relay UI controls** (`relay_control_ui_t`)
- A **master control button UI** (`master_button_ui_t`)
- An **IP status label** at the bottom of the screen

When any relay changes state, the master button can update its appearance to reflect the overall status of all controlled relays, and the IP label can be updated at runtime with `example_lvgl_update_ip_address()`.

## Features

- **Up to 6 relays** with dedicated UI tiles (`Relay 1` … `Relay 6`)
- **Hardware abstraction layer** via `relay_hardware_create()` for each relay:
  - Configurable GPIO for the relay output
  - Configurable GPIO/ADC channel for current/voltage sense (ADC1/ADC2)
- **Master control button** (via `master_button_ui_*` APIs) to act on all relays at once
- **Dynamic IP display**:
  - Shows `IP: --` when not connected
  - Shows `IP: <address>` in green when connected

## Hardware Overview

The example `lvgl_demo_ui` currently assumes:

- **6 relay outputs** on GPIOs:
  - Relay 1: `GPIO_NUM_35`
  - Relay 2: `GPIO_NUM_36`
  - Relay 3: `GPIO_NUM_37`
  - Relay 4: `GPIO_NUM_38`
  - Relay 5: `GPIO_NUM_39`
  - Relay 6: `GPIO_NUM_40`
- **LED / sense pins and ADC channels** (update as needed for your board):
  - Relay 1: LED `GPIO_NUM_48`, ADC1 channel 3
  - Relay 2: LED `GPIO_NUM_21`, ADC1 channel 4
  - Relay 3: LED `GPIO_NUM_2`,  ADC1 channel 5
  - Relay 4: LED `GPIO_NUM_14`, ADC1 channel 6
  - Relay 5: LED `GPIO_NUM_13`, ADC2 channel 0
  - Relay 6: LED `GPIO_NUM_47`, ADC2 channel 1

Adjust these pin and ADC assignments in `example_lvgl_demo_ui()` to match your actual hardware.

## Project Structure (relevant parts)

- `main/lvgl_demo_ui.c` – Creates the LVGL screen, relay tiles, master button, and IP label
- `main/relay_hardware.*` – Relay hardware abstraction (GPIO, ADC, etc.)
- `main/relay_control_ui.*` – LVGL widgets for each relay (on/off, status, feedback)
- `main/master_button_ui.*` – LVGL widget for the master control button

## Building and Flashing

- **Requirements**:
  - ESP-IDF installed and added to your `PATH`
  - Supported ESP32‑series board
  - SPI LCD compatible with your chosen LVGL/`esp_lcd` configuration
- **Build and flash**:

```bash
idf.py set-target <your_target>   # e.g. esp32s3
idf.py -p <PORT> build flash monitor
```

Replace `<PORT>` with the serial port for your board (for example, `tty.usbserial-xxxxx` on macOS).

## Runtime Behavior

On boot, the firmware:

1. Initializes relay hardware objects for all six relays.
2. Creates LVGL relay control widgets and positions them on the screen.
3. (Optionally) wires the master button to control all relays.
4. Creates the IP label at the bottom of the screen.

To update the IP display at runtime, call:

```c
example_lvgl_update_ip_address("192.168.1.10");
```

Passing `NULL` or an empty string will reset the label to `IP: --` and gray text.

## Troubleshooting

- **Relays or LEDs don’t respond**:
  - Verify the GPIO pin numbers and ADC channels in `example_lvgl_demo_ui()` match your actual wiring.
  - Check that the relay board is powered and that any optocouplers/driver circuits are correctly referenced.
- **LVGL UI appears but layout is wrong**:
  - Make sure your display resolution and LVGL configuration match the screen you are using.
  - Adjust the alignment/offsets in `relay_control_ui_create()` calls if needed.

