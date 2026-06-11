/**
 * WebSocket Client Manager - ESP-IDF v5.5.2
 *
 * Features:
 * - Persistent WebSocket connection to cloud server
 * - Auto-reconnect with exponential backoff
 * - JSON command parsing
 * - Device status reporting
 * - Binary audio data streaming
 * - Ping/pong keepalive
 *
 * Dependency: espressif/esp_websocket_client
 * Install: idf.py add-dependency "espressif/esp_websocket_client^1.2.3"
 */

#include "fairy_nest.h"
#include "esp_websocket_client.h"
#include "cJSON.h"

static const char *TAG = TAG_WS;

// =============================================================================
// INTERNAL STATE
// =============================================================================
static esp_websocket_client_handle_t s_ws_client = NULL;
static char s_ws_buffer[WS_BUFFER_SIZE];
static bool s_ws_connected = false;

// Exponential backoff
#define WS_BACKOFF_MS_INITIAL   1000
#define WS_BACKOFF_MS_MAX       60000
static uint32_t s_backoff_ms = WS_BACKOFF_MS_INITIAL;

// =============================================================================
// BUILD AUTH JSON
// =============================================================================
static void send_auth(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "auth");
    cJSON_AddStringToObject(root, "api_key", CONFIG_FAIRYNEST_API_KEY);
    cJSON_AddStringToObject(root, "device_id", g_state.device_id);
    cJSON_AddStringToObject(root, "firmware_ver", g_state.firmware_ver);

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        esp_websocket_client_send_text(s_ws_client, json, strlen(json),
                                        pdMS_TO_TICKS(1000));
        free(json);
    }
    cJSON_Delete(root);
}

// =============================================================================
// BUILD STATUS JSON
// =============================================================================
static void send_status(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "device_status");
    cJSON_AddStringToObject(root, "device_id", g_state.device_id);
    cJSON_AddNumberToObject(root, "wifi_rssi", g_state.wifi_rssi);
    cJSON_AddNumberToObject(root, "csi_variance", g_state.csi_variance);
    cJSON_AddBoolToObject(root, "human_present",
                          g_state.presence_state != PRESENCE_EMPTY);
    cJSON_AddNumberToObject(root, "presence_state", g_state.presence_state);
    cJSON_AddNumberToObject(root, "led_duty", g_state.led_duty);
    cJSON_AddNumberToObject(root, "voice_state", g_state.voice_state);
    cJSON_AddBoolToObject(root, "alarm_ringing", g_state.alarm_ringing);
    cJSON_AddBoolToObject(root, "snooze_active", g_state.alarm_snooze_active);
    cJSON_AddNumberToObject(root, "light_mode", g_state.light_mode);
    cJSON_AddNumberToObject(root, "uptime", g_state.uptime_sec);
    cJSON_AddNumberToObject(root, "breath_rate", g_state.breath_rate);
    cJSON_AddNumberToObject(root, "heap_free",
                            esp_get_free_heap_size());

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        esp_websocket_client_send_text(s_ws_client, json, strlen(json),
                                        pdMS_TO_TICKS(1000));
        free(json);
    }
    cJSON_Delete(root);
}

// =============================================================================
// PARSE CLOUD COMMAND
// =============================================================================
static void handle_command(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "command");
    if (!cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return;
    }

    const char *command = cmd->valuestring;
    ESP_LOGI(TAG, "Received command: %s", command);

    fairy_msg_t msg = {0};

    if (strcmp(command, "set_light") == 0) {
        cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
        if (cJSON_IsNumber(brightness)) {
            msg.type = MSG_LED_SET_BRIGHTNESS;
            msg.param_i = brightness->valueint;
            xQueueSend(g_msg_queue, &msg, 0);
        }
    }
    else if (strcmp(command, "stop_alarm") == 0) {
        msg.type = MSG_ALARM_STOP;
        xQueueSend(g_msg_queue, &msg, 0);
    }
    else if (strcmp(command, "snooze") == 0) {
        msg.type = MSG_ALARM_SNOOZE;
        xQueueSend(g_msg_queue, &msg, 0);
    }
    else if (strcmp(command, "set_csi_threshold") == 0) {
        cJSON *threshold = cJSON_GetObjectItem(root, "threshold");
        if (cJSON_IsNumber(threshold)) {
            csi_set_threshold((float)threshold->valuedouble);
        }
    }
    else if (strcmp(command, "reset") == 0) {
        ESP_LOGW(TAG, "Remote reset command received");
        esp_restart();
    }
    else if (strcmp(command, "tts_play") == 0) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            ESP_LOGI(TAG, "TTS: %s", text->valuestring);
            // TTS playback would be triggered here
        }
    }
    else {
        ESP_LOGW(TAG, "Unknown command: %s", command);
    }

    cJSON_Delete(root);
}

// =============================================================================
// WEBSOCKET EVENT HANDLER
// =============================================================================
static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    (void) handler_args;
    (void) base;

    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            s_ws_connected = true;
            g_state.ws_connected = true;
            s_backoff_ms = WS_BACKOFF_MS_INITIAL;  // Reset backoff
            xEventGroupSetBits(g_event_group, EVENT_WS_CONNECTED);
            xEventGroupClearBits(g_event_group, EVENT_WS_DISCONNECTED);
            send_auth();
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            s_ws_connected = false;
            g_state.ws_connected = false;
            xEventGroupClearBits(g_event_group, EVENT_WS_CONNECTED);
            xEventGroupSetBits(g_event_group, EVENT_WS_DISCONNECTED);
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == WS_TRANSPORT_OPCODES_TEXT &&
                data->data_len > 0) {
                // Null-terminate
                size_t len = data->data_len;
                if (len >= sizeof(s_ws_buffer)) len = sizeof(s_ws_buffer) - 1;
                memcpy(s_ws_buffer, data->data_ptr, len);
                s_ws_buffer[len] = '\0';
                handle_command(s_ws_buffer);
            }
            else if (data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
                // TTS audio data - forward to I2S TX
                // i2s_audio_play((const int16_t *)data->data_ptr,
                //                data->data_len / sizeof(int16_t));
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            break;

        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGW(TAG, "WebSocket closed");
            s_ws_connected = false;
            g_state.ws_connected = false;
            break;

        default:
            break;
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================
esp_err_t websocket_init(void)
{
    // Build WebSocket URI
    char ws_uri[128];
    snprintf(ws_uri, sizeof(ws_uri),
             "ws://%s:%d%s",
             CONFIG_FAIRYNEST_CLOUD_HOST,
             CONFIG_FAIRYNEST_CLOUD_PORT,
             CONFIG_FAIRYNEST_CLOUD_PATH);

    ESP_LOGI(TAG, "WebSocket server: %s", ws_uri);

    esp_websocket_client_config_t ws_cfg = {
        .uri = ws_uri,
        .keep_alive_enable = true,
        .keep_alive_idle = 30,
        .keep_alive_interval = 10,
        .keep_alive_count = 3,
        .reconnect_timeout_ms = 5000,
        .ping_interval_sec = 30,
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (s_ws_client == NULL) {
        ESP_LOGE(TAG, "Failed to init WebSocket client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_websocket_register_events(
        s_ws_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL));

    return ESP_OK;
}

// =============================================================================
// WEBSOCKET TASK
// =============================================================================
void websocket_task(void *pvParameters)
{
    (void) pvParameters;
    ESP_LOGI(TAG, "WebSocket task started on core %d", xPortGetCoreID());


    for (;;) {
        // Wait for WiFi
        EventBits_t bits = xEventGroupWaitBits(
            g_event_group,
            EVENT_WIFI_CONNECTED | EVENT_WIFI_FAIL,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));

        if (!(bits & EVENT_WIFI_CONNECTED)) {
            // WiFi not connected
            if (s_ws_connected) {
                esp_websocket_client_stop(s_ws_client);
            }
            continue;
        }

        // Start WebSocket if not connected
        if (!s_ws_connected) {
            ESP_LOGI(TAG, "Connecting to WebSocket server...");
            esp_err_t ret = esp_websocket_client_start(s_ws_client);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(s_backoff_ms));
                s_backoff_ms = MIN(s_backoff_ms * 2, WS_BACKOFF_MS_MAX);
                continue;
            }
        }

        // Check message queue for outbound messages
        fairy_msg_t msg;
        if (xQueueReceive(g_msg_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (msg.type == MSG_WS_SEND_STATUS && s_ws_connected) {
                send_status();
            }
        }

    }
}

// =============================================================================
// PUBLIC API
// =============================================================================
esp_err_t websocket_send_json(const char *json_str)
{
    if (!s_ws_connected || s_ws_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_websocket_client_send_text(s_ws_client, json_str,
                                              strlen(json_str),
                                              pdMS_TO_TICKS(2000));
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t websocket_send_binary(const uint8_t *data, size_t len)
{
    if (!s_ws_connected || s_ws_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_websocket_client_send_bin(s_ws_client, (const char *)data,
                                             len, pdMS_TO_TICKS(500));
    return (ret >= 0) ? ESP_OK : ESP_FAIL;
}

bool websocket_is_connected(void)
{
    return s_ws_connected;
}
