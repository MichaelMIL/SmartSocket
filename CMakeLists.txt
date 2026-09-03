cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
# Firmware version, reported by /api/debug - bump on every release so OTA updates can be verified
set(PROJECT_VER "1.3.0")
project(spi_lcd_touch)
