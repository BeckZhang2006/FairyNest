# FairyNest Smart Bedside Terminal - ESP-IDF v5.5.2 Project
# Target: ESP32-S3 with WiFi CSI + Voice AI + PWM LED

cmake_minimum_required(VERSION 3.16)

# Include project-specific sdkconfig defaults
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(fairy_nest)
