/**
 * Alarm System - ESP-IDF v5.5.2
 *
 * Features:
 * - Up to 5 configurable alarms
 * - Weekly schedule support (day bitmask)
 * - Gradual sunrise wake (PWM brightness fade)
 * - Smart snooze with CSI-based presence detection
 * - Voice-negotiated snooze
 * - NVS persistence
 */

#include "fairy_nest.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = TAG_ALARM;

// =============================================================================
// INTERNAL STRUCTURES
// =============================================================================
typedef struct {
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    uint8_t days;       // Bitmask: bit0=Sun, bit1=Mon, ..., bit6=Sat
    char label[32];
    bool is_smart_wake; // Use CSI for smart wake
} alarm_entry_t;

// =============================================================================
// INTERNAL STATE
// =============================================================================
static alarm_entry_t s_alarms[ALARM_MAX_COUNT];
static bool s_ringing = false;
static bool s_snooze_active = false;
static uint32_t s_snooze_end_ms = 0;
static uint32_t s_alarm_start_ms = 0;
static uint32_t s_alarm_trigger_minute = 0xFFFF;  // Minute of day last triggered

// =============================================================================
// GET TIME FROM SNTP
// =============================================================================
static bool get_local_time(struct tm *out_tm)
{
    time_t now = 0;
    struct tm timeinfo = {0};

    time(&now);
    localtime_r(&now, &timeinfo);

    // Check if time is valid (year >= 2024)
    if (timeinfo.tm_year < 124) {
        return false;
    }

    *out_tm = timeinfo;
    return true;
}

// =============================================================================
// INITIALIZATION
// =============================================================================
esp_err_t alarm_init(void)
{
    ESP_LOGI(TAG, "Alarm system initializing");

    // Initialize SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_init();

    // Set timezone (default China GMT+8)
    setenv("TZ", "CST-8", 1);
    tzset();

    // Load alarms from NVS
    for (int i = 0; i < ALARM_MAX_COUNT; i++) {
        char key[16];
        snprintf(key, sizeof(key), "alarm%d_en", i);
        int32_t enabled = 0;
        config_get_int(key, &enabled, 0);
        s_alarms[i].enabled = (enabled != 0);

        snprintf(key, sizeof(key), "alarm%d_h", i);
        config_get_int(key, (int32_t *)&s_alarms[i].hour, 7);

        snprintf(key, sizeof(key), "alarm%d_m", i);
        config_get_int(key, (int32_t *)&s_alarms[i].minute, 30);

        snprintf(key, sizeof(key), "alarm%d_d", i);
        config_get_int(key, (int32_t *)&s_alarms[i].days, 0x7E); // Mon-Fri

        snprintf(key, sizeof(key), "alarm%d_l", i);
        config_get_string(key, s_alarms[i].label,
                          sizeof(s_alarms[i].label), "Alarm");

        s_alarms[i].is_smart_wake = true;
    }

    ESP_LOGI(TAG, "Alarm system initialized (%d alarms)", ALARM_MAX_COUNT);
    return ESP_OK;
}

// =============================================================================
// ALARM TASK
// =============================================================================
void alarm_task(void *pvParameters)
{
    (void) pvParameters;
    ESP_LOGI(TAG, "Alarm task started on core %d", xPortGetCoreID());

    TickType_t last_wake = xTaskGetTickCount();
    struct tm timeinfo;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));  // 1 Hz check

        // ---- Check messages ----
        fairy_msg_t msg;
        if (xQueueReceive(g_msg_queue, &msg, 0) == pdTRUE) {
            switch (msg.type) {
                case MSG_ALARM_STOP:
                    if (s_ringing) {
                        s_ringing = false;
                        s_snooze_active = false;
                        g_state.alarm_ringing = false;
                        g_state.alarm_snooze_active = false;
                        led_stop_alarm_fade();
                        ESP_LOGI(TAG, "Alarm stopped");
                    }
                    break;

                case MSG_ALARM_SNOOZE:
                    if (s_ringing) {
                        s_snooze_active = true;
                        s_snooze_end_ms = xTaskGetTickCount() * portTICK_PERIOD_MS
                                         + ALARM_SNOOZE_MS_DEFAULT;
                        g_state.alarm_snooze_active = true;
                        g_state.alarm_ringing = false;
                        s_ringing = false;
                        led_stop_alarm_fade();
                        ESP_LOGI(TAG, "Snooze for %d min",
                                 ALARM_SNOOZE_MS_DEFAULT / 60000);
                    }
                    break;

                case MSG_ALARM_TRIGGER:
                    if (!s_ringing) {
                        s_ringing = true;
                        s_alarm_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
                        g_state.alarm_ringing = true;
                        g_state.alarm_snooze_active = false;
                        led_start_alarm_fade(ALARM_FADE_MS_DEFAULT);
                        i2s_play_alarm_sound();
                        ESP_LOGI(TAG, "Alarm triggered!");
                    }
                    break;

                default:
                    break;
            }
        }

        // ---- Check snooze ----
        if (s_snooze_active) {
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (now_ms >= s_snooze_end_ms) {
                ESP_LOGI(TAG, "Snooze ended, resuming alarm");
                s_snooze_active = false;
                g_state.alarm_snooze_active = false;
                fairy_msg_t trigger = {.type = MSG_ALARM_TRIGGER};
                xQueueSend(g_msg_queue, &trigger, 0);
            }
            continue;
        }

        // ---- Skip if alarm already ringing ----
        if (s_ringing) {
            // Check if user got up (CSI detects presence change to moving)
            if (g_state.presence_state == PRESENCE_MOVING) {
                ESP_LOGI(TAG, "User got up, stopping alarm");
                fairy_msg_t stop = {.type = MSG_ALARM_STOP};
                xQueueSend(g_msg_queue, &stop, 0);
            }
            // Auto-snooze if still in bed after 1 minute
            else {
                uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS)
                                 - s_alarm_start_ms;
                if (elapsed > ALARM_AUTO_SNOOZE_MS) {
                    ESP_LOGI(TAG, "User still in bed, auto-snooze");
                    fairy_msg_t snooze = {.type = MSG_ALARM_SNOOZE};
                    xQueueSend(g_msg_queue, &snooze, 0);
                }
            }
            continue;
        }

        // ---- Check alarm times ----
        if (!get_local_time(&timeinfo)) {
            continue;  // Time not synced yet
        }

        uint8_t current_hour = timeinfo.tm_hour;
        uint8_t current_min = timeinfo.tm_min;
        uint8_t current_sec = timeinfo.tm_sec;
        uint8_t current_wday = timeinfo.tm_wday; // 0=Sun, 6=Sat
        uint16_t current_minute_of_day = current_hour * 60 + current_min;

        // Only check at the start of each minute
        if (current_sec != 0) {
            continue;
        }

        // Prevent duplicate triggers
        if (current_minute_of_day == s_alarm_trigger_minute) {
            continue;
        }

        for (int i = 0; i < ALARM_MAX_COUNT; i++) {
            if (!s_alarms[i].enabled) continue;

            // Check day of week
            if (!(s_alarms[i].days & (1 << current_wday))) continue;

            // Check time
            if (s_alarms[i].hour == current_hour &&
                s_alarms[i].minute == current_min) {

                s_alarm_trigger_minute = current_minute_of_day;
                ESP_LOGI(TAG, "Alarm '%s' triggered at %02d:%02d",
                         s_alarms[i].label, current_hour, current_min);

                fairy_msg_t trigger = {.type = MSG_ALARM_TRIGGER};
                xQueueSend(g_msg_queue, &trigger, 0);
                break;
            }
        }
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================
esp_err_t alarm_set(uint8_t index, bool enabled, uint8_t hour, uint8_t minute,
                    uint8_t days, const char *label)
{
    if (index >= ALARM_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_alarms[index].enabled = enabled;
    s_alarms[index].hour = hour;
    s_alarms[index].minute = minute;
    s_alarms[index].days = days;
    if (label) {
        strncpy(s_alarms[index].label, label, sizeof(s_alarms[index].label) - 1);
        s_alarms[index].label[sizeof(s_alarms[index].label) - 1] = '\0';
    }

    // Persist to NVS
    char key[16];
    snprintf(key, sizeof(key), "alarm%d_en", index);
    config_save_int(key, enabled ? 1 : 0);
    snprintf(key, sizeof(key), "alarm%d_h", index);
    config_save_int(key, hour);
    snprintf(key, sizeof(key), "alarm%d_m", index);
    config_save_int(key, minute);
    snprintf(key, sizeof(key), "alarm%d_d", index);
    config_save_int(key, days);
    if (label) {
        snprintf(key, sizeof(key), "alarm%d_l", index);
        config_save_string(key, label);
    }

    ESP_LOGI(TAG, "Alarm %d set: %02d:%02d, days=0x%02X, label=%s",
             index, hour, minute, days, label ? label : "Alarm");

    return ESP_OK;
}

esp_err_t alarm_stop(void)
{
    fairy_msg_t msg = {.type = MSG_ALARM_STOP};
    xQueueSend(g_msg_queue, &msg, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t alarm_snooze(void)
{
    fairy_msg_t msg = {.type = MSG_ALARM_SNOOZE};
    xQueueSend(g_msg_queue, &msg, portMAX_DELAY);
    return ESP_OK;
}
