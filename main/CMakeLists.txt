idf_component_register(SRCS "spi_lcd_touch_example_main.c" "lvgl_demo_ui.c" "components/relay_control_ui/relay_control_ui.c" "components/relay_control_ui/master_button_ui.c" "components/relay_control_ui/relay_hardware.c" "components/wifi_ota/wifi_ota.c" "components/wifi_ota/http_server.c"
                      INCLUDE_DIRS "." "components/relay_control_ui" "components/wifi_ota"
                      REQUIRES esp_adc esp_wifi esp_https_ota app_update nvs_flash esp_http_server spiffs)

# Embed web files into SPIFFS
spiffs_create_partition_image(spiffs "${CMAKE_CURRENT_SOURCE_DIR}/web" FLASH_IN_PROJECT)
