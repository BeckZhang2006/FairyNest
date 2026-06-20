# FairyNest Smart Bedside Terminal - ESP-IDF v5.5.2 Project
# Target: ESP32-S3 with WiFi CSI + Voice AI + PWM LED

cmake_minimum_required(VERSION 3.16)

# Include project-specific sdkconfig defaults.
# sdkconfig.defaults.local contains secrets (WiFi, API keys) and is gitignored.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(fairy_nest)

# Workaround for GCC 14.2.0 internal compiler error (segmentation fault)
# in components/esp_lcd/rgb/esp_lcd_panel_rgb.c when compiled with -Og.
# Compile the esp_lcd component with -O0 to avoid the crash.
# This component is not used by FairyNest, so the performance impact is negligible.
idf_component_get_property(esp_lcd_lib esp_lcd COMPONENT_LIB)
target_compile_options(${esp_lcd_lib} PRIVATE -O0)
