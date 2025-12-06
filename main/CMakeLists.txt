idf_component_register(SRCS "spi_lcd_touch_example_main.c" "lvgl_demo_ui.c" "components/relay_control_ui/relay_control_ui.c" "components/relay_control_ui/master_button_ui.c" "components/relay_control_ui/relay_hardware.c"
                      INCLUDE_DIRS "." "components/relay_control_ui"
                      REQUIRES esp_adc)
