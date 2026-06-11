/**
 * FairyNest - Smart Bedside Terminal
 * ESP-IDF v5.5.2 - ESP32-S3
 *
 * Main entry point. Initializes all subsystems and creates FreeRTOS tasks.
 *
 * Architecture:
 *   - Main Task:       System init, watchdog, status reporting
 *   - WiFi+CSI Task:   WiFi connection, CSI collection, presence detection
 *   - I2S RX Task:     Microphone audio capture
 *   - I2S TX Task:     Speaker audio playback
 *   - LED Task:        PWM control, fading, breathing effects
 *   - WebSocket Task:  Cloud communication
 *   - Voice Task:      Voice state machine, VAD, command processing
 *   - Alarm Task:      Alarm scheduling, smart wake logic
 */

#include "fairy_nest.h"

// =============================================================================
// GLOBALS
// =============================================================================
EventGroupHandle_t  g_event_group = NULL;
QueueHandle_t       g_msg_queue   = NULL;
SemaphoreHandle_t   g_state_mutex = NULL;
system_state_t      g_state       = {0};

static const char *TAG = TAG_MAIN;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================
static esp_err_t system_init(void);
static void      main_task(void *pvParameters);

// =============================================================================
// APP MAIN - Entry Point
// =============================================================================
void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  FairyNest v%s - Smart Bedside Terminal", FAIRY_NEST_VERSION);
    ESP_LOGI(TAG, "  ESP-IDF %s | ESP32-S3", esp_get_idf_version());
    ESP_LOGI(TAG, "  Build: %s", FAIRY_NEST_BUILD_DATE);
    ESP_LOGI(TAG, "========================================");

    // Initialize all subsystems
    esp_err_t ret = system_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System init failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "All subsystems initialized successfully");

    // Create main task
    BaseType_t xReturned = xTaskCreatePinnedToCore(
        main_task,
        "main_task",
        TASK_STACK_MAIN,
        NULL,
        TASK_PRIORITY_MAIN,
        NULL,
        0   // Run on PRO_CPU (core 0)
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create main task");
    }
}

// =============================================================================
// SYSTEM INITIALIZATION
// =============================================================================
static esp_err_t system_init(void)
{
    esp_err_t ret;

    // ---- 1. NVS Flash (for persistent config) ----
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[OK] NVS flash initialized");

    // ---- 2. Create synchronization primitives ----
    g_event_group = xEventGroupCreate();
    if (g_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    g_msg_queue = xQueueCreate(MSG_QUEUE_SIZE, sizeof(fairy_msg_t));
    if (g_msg_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create message queue");
        return ESP_ERR_NO_MEM;
    }

    g_state_mutex = xSemaphoreCreateMutex();
    if (g_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "[OK] FreeRTOS primitives created");

    // ---- 3. Configuration Manager ----
    ret = config_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Config init warning: %s (using defaults)", esp_err_to_name(ret));
    }
    ret = config_load();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Config load warning: %s (using defaults)", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "[OK] Configuration loaded");

    // ---- 4. Initialize global state ----
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_state.device_id, sizeof(g_state.device_id),
             "FAIRY_%02X%02X%02X", mac[3], mac[4], mac[5]);
    strncpy(g_state.firmware_ver, FAIRY_NEST_VERSION,
            sizeof(g_state.firmware_ver) - 1);
    g_state.csi_threshold = CSI_PRESENCE_THRESHOLD_DEFAULT;
    g_state.csi_enabled = CSI_ENABLED;
    ESP_LOGI(TAG, "Device ID: %s", g_state.device_id);

    // ---- 5. Status LED ----
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(STATUS_LED_GPIO, 0);

    // ---- 6. Initialize subsystems (order matters) ----

    // LED PWM (no dependencies)
    ret = led_pwm_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED PWM init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[OK] LED PWM initialized");

    // I2S Audio (no dependencies)
    ret = i2s_audio_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2S audio init failed: %s", esp_err_to_name(ret));
        // Non-fatal: continue without audio
    } else {
        ESP_LOGI(TAG, "[OK] I2S audio initialized");
    }

    // WiFi + CSI (depends on esp_netif, esp_event)
    ret = wifi_csi_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi+CSI init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[OK] WiFi+CSI initialized");

    // WebSocket client (depends on WiFi)
    ret = websocket_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WebSocket init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "[OK] WebSocket client initialized");
    }

    // Voice FSM (depends on I2S)
    ret = voice_fsm_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Voice FSM init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "[OK] Voice FSM initialized");
    }

    // Alarm system
    ret = alarm_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Alarm init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "[OK] Alarm system initialized");
    }

    // ---- 7. Create all FreeRTOS tasks ----
    BaseType_t xReturned;

    // WiFi + CSI Task (Core 0)
    xReturned = xTaskCreatePinnedToCore(
        wifi_csi_task, "wifi_csi", TASK_STACK_CSI, NULL,
        TASK_PRIORITY_CSI, NULL, 0);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi+CSI task");
    }

    // I2S RX Task (Core 1 - audio processing on APP_CPU)
    xReturned = xTaskCreatePinnedToCore(
        i2s_rx_task, "i2s_rx", TASK_STACK_I2S_RX, NULL,
        TASK_PRIORITY_I2S, NULL, 1);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create I2S RX task");
    }

    // I2S TX Task (Core 1)
    xReturned = xTaskCreatePinnedToCore(
        i2s_tx_task, "i2s_tx", TASK_STACK_I2S_TX, NULL,
        TASK_PRIORITY_I2S, NULL, 1);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create I2S TX task");
    }

    // LED Task (Core 1)
    xReturned = xTaskCreatePinnedToCore(
        led_task, "led_pwm", TASK_STACK_LED, NULL,
        TASK_PRIORITY_LED, NULL, 1);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED task");
    }

    // WebSocket Task (Core 0)
    xReturned = xTaskCreatePinnedToCore(
        websocket_task, "websocket", TASK_STACK_WS, NULL,
        TASK_PRIORITY_WS, NULL, 0);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WebSocket task");
    }

    // Voice Task (Core 1)
    xReturned = xTaskCreatePinnedToCore(
        voice_task, "voice_fsm", TASK_STACK_VOICE, NULL,
        TASK_PRIORITY_VOICE, NULL, 1);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Voice task");
    }

    // Alarm Task (Core 0)
    xReturned = xTaskCreatePinnedToCore(
        alarm_task, "alarm_sys", TASK_STACK_ALARM, NULL,
        TASK_PRIORITY_ALARM, NULL, 0);
    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Alarm task");
    }

    ESP_LOGI(TAG, "All tasks created successfully");
    ESP_LOGI(TAG, "Say \"Hi Fairy\" to wake me up");

    return ESP_OK;
}

// =============================================================================
// MAIN TASK - System heartbeat and status reporting
// =============================================================================
static void main_task(void *pvParameters)
{
    (void) pvParameters;
    ESP_LOGI(TAG, "Main task started on core %d", xPortGetCoreID());

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick_count = 0;

    for (;;) {
        tick_count++;

        // Update uptime
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_state.uptime_sec = xTaskGetTickCount() / configTICK_RATE_HZ;
            xSemaphoreGive(g_state_mutex);
        }

        // Blink status LED (1Hz when normal, 2Hz when WiFi connected)
        bool wifi_ok = (xEventGroupGetBits(g_event_group) & EVENT_WIFI_CONNECTED) != 0;
        if (tick_count % (wifi_ok ? 25 : 50) == 0) {
            static bool led_state = false;
            led_state = !led_state;
            gpio_set_level(STATUS_LED_GPIO, led_state ? 1 : 0);
        }

        // Periodic status log (every 10 seconds)
        if (tick_count % 100 == 0) {
            ESP_LOGI(TAG, "Uptime: %lus | WiFi: %s | WS: %s | CSI: %.1f | Presence: %d",
                     g_state.uptime_sec,
                     wifi_ok ? "OK" : "---",
                     g_state.ws_connected ? "OK" : "---",
                     g_state.csi_variance,
                     g_state.presence_state);
        }

        // Send device status to cloud every 5 seconds
        if (tick_count % 50 == 0 && g_state.ws_connected) {
            fairy_msg_t msg = {.type = MSG_WS_SEND_STATUS};
            xQueueSend(g_msg_queue, &msg, 0);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));  // 10 Hz tick
    }
}
