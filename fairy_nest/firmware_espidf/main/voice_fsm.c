/**
 * Voice FSM (Finite State Machine) - ESP-IDF v5.5.2
 *
 * Manages the voice interaction lifecycle:
 *   IDLE -> WAKE_DETECTED -> RECORDING -> PROCESSING -> PLAYING -> IDLE
 *
 * Features:
 * - Wake word energy-based detection trigger
 * - Recording state management
 * - Audio streaming to cloud
 * - Local command processing (light, alarm)
 * - Forward complex commands to LLM via WebSocket
 */

#include "fairy_nest.h"
#include "cJSON.h"

static const char *TAG = TAG_VOICE;

// =============================================================================
// STATE MACHINE
// =============================================================================
static voice_state_t s_state = VOICE_STATE_IDLE;
static uint32_t s_state_enter_ms = 0;
static uint32_t s_silence_start_ms = 0;
static uint32_t s_last_speech_ms = 0;

// =============================================================================
// INITIALIZATION
// =============================================================================
esp_err_t voice_fsm_init(void)
{
    s_state = VOICE_STATE_IDLE;
    s_state_enter_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_state.voice_state = VOICE_STATE_IDLE;

    ESP_LOGI(TAG, "Voice FSM initialized");
    ESP_LOGI(TAG, "Wake word: \"%s\"", WAKE_WORD);

    return ESP_OK;
}

// =============================================================================
// STATE TRANSITION
// =============================================================================
static void transition_to(voice_state_t new_state)
{
    const char *state_names[] = {"IDLE", "WAKE", "RECORDING", "PROCESSING", "PLAYING"};
    ESP_LOGI(TAG, "State: %s -> %s",
             state_names[s_state], state_names[new_state]);

    s_state = new_state;
    s_state_enter_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    g_state.voice_state = new_state;
}

// =============================================================================
// SEND VOICE EVENT TO CLOUD
// =============================================================================
static void send_voice_event(const char *event)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "voice_event");
    cJSON_AddStringToObject(root, "event", event);

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        websocket_send_json(json);
        free(json);
    }
    cJSON_Delete(root);
}

// =============================================================================
// SEND VOICE COMMAND TO CLOUD
// =============================================================================
static void send_voice_command(const char *text)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "voice_command");
    cJSON_AddStringToObject(root, "text", text);

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        websocket_send_json(json);
        free(json);
    }
    cJSON_Delete(root);
}

// =============================================================================
// LOCAL COMMAND PROCESSING
// =============================================================================
static void process_local_command(const char *text)
{
    fairy_msg_t msg = {0};
    bool handled = false;

    // Light commands
    if (strstr(text, "开灯") || strstr(text, "打开灯") ||
        strstr(text, "light on")) {
        msg.type = MSG_LED_SET_BRIGHTNESS;
        msg.param_i = 100;
        xQueueSend(g_msg_queue, &msg, 0);
        ESP_LOGI(TAG, "Local: Turn light ON");
        handled = true;
    }
    else if (strstr(text, "关灯") || strstr(text, "关闭灯") ||
             strstr(text, "light off")) {
        msg.type = MSG_LED_OFF;
        xQueueSend(g_msg_queue, &msg, 0);
        ESP_LOGI(TAG, "Local: Turn light OFF");
        handled = true;
    }
    // Alarm commands
    else if (strstr(text, "停止闹钟") || strstr(text, "stop alarm")) {
        msg.type = MSG_ALARM_STOP;
        xQueueSend(g_msg_queue, &msg, 0);
        ESP_LOGI(TAG, "Local: Stop alarm");
        handled = true;
    }
    else if (strstr(text, "贪睡") || strstr(text, "snooze") ||
             strstr(text, "再睡")) {
        msg.type = MSG_ALARM_SNOOZE;
        xQueueSend(g_msg_queue, &msg, 0);
        ESP_LOGI(TAG, "Local: Snooze alarm");
        handled = true;
    }

    if (handled) {
        // Play confirmation tone
        i2s_play_beep(1);
    }
}

// =============================================================================
// VOICE TASK - Main FSM loop
// =============================================================================
void voice_task(void *pvParameters)
{
    (void) pvParameters;
    ESP_LOGI(TAG, "Voice task started on core %d", xPortGetCoreID());

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50));  // 20 Hz FSM update

        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // ---- Process messages from queue ----
        fairy_msg_t msg;
        if (xQueueReceive(g_msg_queue, &msg, 0) == pdTRUE) {
            switch (msg.type) {
                case MSG_VOICE_WAKE_DETECTED:
                    if (s_state == VOICE_STATE_IDLE) {
                        transition_to(VOICE_STATE_WAKE);
                    }
                    break;

                case MSG_VOICE_START_RECORD:
                    if (s_state == VOICE_STATE_WAKE ||
                        s_state == VOICE_STATE_IDLE) {
                        transition_to(VOICE_STATE_RECORDING);
                        i2s_audio_start_record();
                        send_voice_event("recording_started");
                    }
                    break;

                case MSG_VOICE_STOP_RECORD:
                    if (s_state == VOICE_STATE_RECORDING) {
                        i2s_audio_stop_record();
                        send_voice_event("recording_finished");
                        transition_to(VOICE_STATE_PROCESSING);
                    }
                    break;

                case MSG_VOICE_VAD_SILENCE:
                    if (s_state == VOICE_STATE_RECORDING) {
                        ESP_LOGI(TAG, "VAD silence detected, stopping recording");
                        i2s_audio_stop_record();
                        send_voice_event("recording_finished");
                        transition_to(VOICE_STATE_PROCESSING);
                    }
                    break;

                case MSG_VOICE_STT_RESULT:
                    if (s_state == VOICE_STATE_PROCESSING) {
                        voice_handle_stt_result(msg.param_s);
                    }
                    break;

                case MSG_VOICE_PLAY_TTS:
                    if (s_state == VOICE_STATE_PROCESSING) {
                        transition_to(VOICE_STATE_PLAYING);
                    }
                    break;

                case MSG_VOICE_TTS_END:
                    if (s_state == VOICE_STATE_PLAYING) {
                        transition_to(VOICE_STATE_IDLE);
                    }
                    break;

                default:
                    break;
            }
        }

        // ---- State machine logic ----
        switch (s_state) {
            case VOICE_STATE_IDLE:
                // Wait for wake word detection (handled by message)
                // Background: I2S RX task runs VAD
                break;

            case VOICE_STATE_WAKE: {
                // Short delay after wake, then start recording
                if (now_ms - s_state_enter_ms > 300) {
                    transition_to(VOICE_STATE_RECORDING);
                    i2s_audio_start_record();
                    send_voice_event("recording_started");
                    s_silence_start_ms = 0;
                    s_last_speech_ms = now_ms;
                    ESP_LOGI(TAG, "Recording started");
                }
                break;
            }

            case VOICE_STATE_RECORDING: {
                // Monitor recording duration
                uint32_t record_duration = now_ms - s_state_enter_ms;

                // Check max recording duration
                if (record_duration > VAD_MAX_RECORD_MS) {
                    ESP_LOGI(TAG, "Max recording duration reached");
                    i2s_audio_stop_record();
                    send_voice_event("recording_finished");
                    transition_to(VOICE_STATE_PROCESSING);
                    break;
                }
                break;
            }

            case VOICE_STATE_PROCESSING: {
                // Wait for server voice_result or TTS events
                if (now_ms - s_state_enter_ms > 15000) {
                    ESP_LOGW(TAG, "Processing timeout, returning to idle");
                    transition_to(VOICE_STATE_IDLE);
                }
                break;
            }

            case VOICE_STATE_PLAYING: {
                // Wait for TTS playback to complete (tts_end message)
                if (now_ms - s_state_enter_ms > 60000) {
                    ESP_LOGW(TAG, "TTS playback timeout, returning to idle");
                    transition_to(VOICE_STATE_IDLE);
                }
                break;
            }

            default:
                break;
        }
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================
esp_err_t voice_handle_wake(void)
{
    fairy_msg_t msg = {.type = MSG_VOICE_WAKE_DETECTED};
    xQueueSend(g_msg_queue, &msg, portMAX_DELAY);
    return ESP_OK;
}

void voice_process_text(const char *text)
{
    if (text == NULL || strlen(text) == 0) return;

    ESP_LOGI(TAG, "Processing: \"%s\"", text);

    // Try local command first
    process_local_command(text);

    // Always forward to cloud for LLM processing
    send_voice_command(text);
}

void voice_handle_stt_result(const char *text)
{
    if (text == NULL || strlen(text) == 0) return;

    ESP_LOGI(TAG, "STT result: \"%s\"", text);

    // Only process local commands, do NOT forward to cloud again
    // to avoid infinite loop (server already processed this text)
    process_local_command(text);

    // State remains PROCESSING until tts_start or timeout
}
