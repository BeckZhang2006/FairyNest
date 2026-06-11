# FairyNest Main Component
# ESP-IDF v5.5.2 - ESP32-S3

idf_component_register(
    SRCS
        "fairy_nest_main.c"
        "wifi_csi.c"
        "i2s_audio.c"
        "led_pwm.c"
        "websocket_mgr.c"
        "voice_fsm.c"
        "alarm_sys.c"
        "config_mgr.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_websocket_client
        esp_http_client
        json
        spiffs
        esp_timer
    PRIV_REQUIRES
        esp_driver_i2s
        esp_driver_ledc
        esp_driver_gpio
)

# Add WebSocket client component dependency (ESP-IDF v5.5+)
# Run: idf.py add-dependency "espressif/esp_websocket_client^1.2.3"
