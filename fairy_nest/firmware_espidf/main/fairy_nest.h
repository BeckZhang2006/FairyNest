/**
 * FairyNest - Smart Bedside Terminal
 * ESP-IDF v5.5.2 - ESP32-S3
 *
 * Common header with shared definitions, configuration, and inter-module
 * communication structures.
 */

#ifndef FAIRY_NEST_H
#define FAIRY_NEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "esp_sntp.h"

#include "driver/gpio.h"
#include "driver/i2s.h"
#include "driver/ledc.h"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// =============================================================================
// VERSION
// =============================================================================
#define FAIRY_NEST_VERSION      "1.0.0"
#define FAIRY_NEST_BUILD_DATE   __DATE__

// =============================================================================
// LOG TAGS
// =============================================================================
#define TAG_MAIN    "FAIRY_MAIN"
#define TAG_WIFI    "FAIRY_WIFI"
#define TAG_CSI     "FAIRY_CSI"
#define TAG_I2S     "FAIRY_I2S"
#define TAG_LED     "FAIRY_LED"
#define TAG_WS      "FAIRY_WS"
#define TAG_VOICE   "FAIRY_VOICE"
#define TAG_ALARM   "FAIRY_ALARM"
#define TAG_CFG     "FAIRY_CFG"

// =============================================================================
// PIN DEFINITIONS (ESP32-S3-DevKitC-1)
// =============================================================================

// I2S Microphone (INMP441)
#define I2S_MIC_GPIO_SCK        GPIO_NUM_6
#define I2S_MIC_GPIO_WS         GPIO_NUM_5
#define I2S_MIC_GPIO_SD         GPIO_NUM_7
#define I2S_MIC_PORT            I2S_NUM_0

// I2S Speaker (MAX98357A)
#define I2S_SPK_GPIO_BCLK       GPIO_NUM_15
#define I2S_SPK_GPIO_LRC        GPIO_NUM_16
#define I2S_SPK_GPIO_DIN        GPIO_NUM_8
#define I2S_SPK_PORT            I2S_NUM_1

// LED PWM
#define LED_GPIO_PIN            GPIO_NUM_4
#define LED_PWM_TIMER           LEDC_TIMER_0
#define LED_PWM_CHANNEL         LEDC_CHANNEL_0
#define LED_PWM_MODE            LEDC_LOW_SPEED_MODE
#define LED_PWM_FREQ_HZ         5000
#define LED_PWM_RESOLUTION      LEDC_TIMER_12_BIT
#define LED_PWM_MAX_DUTY        (4095)  // 2^12 - 1

// Status LED (onboard)
#define STATUS_LED_GPIO         GPIO_NUM_2

// =============================================================================
// AUDIO CONFIGURATION
// =============================================================================
#define AUDIO_SAMPLE_RATE       16000
#define AUDIO_SAMPLE_BITS       I2S_BITS_PER_SAMPLE_16BIT
#define AUDIO_CHANNELS          1       // Mono
#define AUDIO_BUFFER_SAMPLES    512
#define AUDIO_BUFFER_SIZE       (AUDIO_BUFFER_SAMPLES * sizeof(int16_t))
#define AUDIO_DMA_BUF_COUNT     4
#define AUDIO_DMA_BUF_LEN       512

// =============================================================================
// CSI CONFIGURATION
// =============================================================================
#define CSI_ENABLED             1
#define CSI_SAMPLE_COUNT        128     // Subcarriers
#define CSI_HISTORY_DEPTH       50      // History buffer size
#define CSI_SAMPLE_INTERVAL_MS  10      // 100 Hz effective rate
#define CSI_PRESENCE_THRESHOLD_DEFAULT  15.0f
#define CSI_MOTION_THRESHOLD_DEFAULT    25.0f
#define CSI_BREATH_MIN_HZ       0.1f    // 6 breaths/min
#define CSI_BREATH_MAX_HZ       0.5f    // 30 breaths/min
#define CSI_PRESENCE_COOLDOWN_MS        5000
#define CSI_CALIBRATION_DURATION_MS     3000

// =============================================================================
// VOICE CONFIGURATION
// =============================================================================
#define VAD_ENABLED             1
#define VAD_FRAME_SAMPLES       320     // 20ms at 16kHz
#define VAD_ENERGY_THRESHOLD    500
#define VAD_SILENCE_TIMEOUT_MS  1500
#define VAD_MAX_RECORD_MS       10000
#define WAKE_WORD               "Hi Fairy"
#define WAKE_ENERGY_MULTIPLIER  3       // 3x normal VAD threshold

// =============================================================================
// ALARM CONFIGURATION
// =============================================================================
#define ALARM_MAX_COUNT         5
#define ALARM_SNOOZE_MS_DEFAULT (5 * 60 * 1000)   // 5 minutes
#define ALARM_FADE_MS_DEFAULT   (10 * 60 * 1000)  // 10 minutes
#define ALARM_AUTO_SNOOZE_MS    60000               // 1 minute then auto-snooze

// =============================================================================
// LED CONFIGURATION
// =============================================================================
#define LED_NIGHT_BRIGHTNESS_DEFAULT    200     // 0-4095
#define LED_FADE_MS_DEFAULT             2000
#define LED_BREATH_PERIOD_MS            4000

// =============================================================================
// WIFI / CLOUD CONFIGURATION
// =============================================================================
#define WIFI_CONNECT_TIMEOUT_MS         30000
#define WIFI_RETRY_MAX                  5

#define WS_RECONNECT_INTERVAL_MS        5000
#define WS_PING_INTERVAL_MS             30000
#define WS_BUFFER_SIZE                  4096

// =============================================================================
// TASK CONFIGURATION
// =============================================================================
#define TASK_STACK_MAIN         8192
#define TASK_STACK_WIFI         4096
#define TASK_STACK_CSI          4096
#define TASK_STACK_I2S_RX       8192
#define TASK_STACK_I2S_TX       4096
#define TASK_STACK_LED          4096
#define TASK_STACK_WS           8192
#define TASK_STACK_VOICE        4096
#define TASK_STACK_ALARM        4096

#define TASK_PRIORITY_MAIN      5
#define TASK_PRIORITY_WIFI      4
#define TASK_PRIORITY_CSI       3
#define TASK_PRIORITY_I2S       4
#define TASK_PRIORITY_LED       2
#define TASK_PRIORITY_WS        4
#define TASK_PRIORITY_VOICE     4
#define TASK_PRIORITY_ALARM     3

// =============================================================================
// EVENT GROUP BITS
// =============================================================================
#define EVENT_WIFI_CONNECTED    BIT0
#define EVENT_WIFI_FAIL         BIT1
#define EVENT_WS_CONNECTED      BIT2
#define EVENT_WS_DISCONNECTED   BIT3
#define EVENT_CSI_CALIBRATED    BIT4
#define EVENT_WAKE_WORD         BIT5
#define EVENT_RECORD_DONE       BIT6

// =============================================================================
// INTER-TASK MESSAGE QUEUE
// =============================================================================
#define MSG_QUEUE_SIZE          16

typedef enum {
    MSG_NONE = 0,
    // LED messages
    MSG_LED_ON,
    MSG_LED_OFF,
    MSG_LED_SET_BRIGHTNESS,
    MSG_LED_NIGHT_MODE,
    MSG_LED_ALARM_FADE_START,
    MSG_LED_ALARM_FADE_STOP,
    // Voice messages
    MSG_VOICE_WAKE_DETECTED,
    MSG_VOICE_START_RECORD,
    MSG_VOICE_STOP_RECORD,
    MSG_VOICE_PLAY_TTS,
    // Alarm messages
    MSG_ALARM_TRIGGER,
    MSG_ALARM_STOP,
    MSG_ALARM_SNOOZE,
    // CSI messages
    MSG_CSI_PRESENCE_CHANGED,
    // WebSocket messages
    MSG_WS_COMMAND,
    MSG_WS_SEND_STATUS,
} msg_type_t;

typedef struct {
    msg_type_t type;
    int32_t    param_i;
    float      param_f;
    char       param_s[128];
} fairy_msg_t;

// =============================================================================
// GLOBAL STATE STRUCTURE
// =============================================================================

typedef enum {
    PRESENCE_EMPTY = 0,
    PRESENCE_LYING,
    PRESENCE_SITTING,
    PRESENCE_MOVING,
} presence_state_t;

typedef enum {
    LIGHT_MODE_OFF = 0,
    LIGHT_MODE_NIGHT,
    LIGHT_MODE_ALARM_FADE,
    LIGHT_MODE_MANUAL,
    LIGHT_MODE_BREATHING,
} light_mode_t;

typedef enum {
    VOICE_STATE_IDLE = 0,
    VOICE_STATE_WAKE,
    VOICE_STATE_RECORDING,
    VOICE_STATE_PROCESSING,
    VOICE_STATE_PLAYING,
} voice_state_t;

typedef struct {
    // Device
    char        device_id[18];      // MAC address string
    char        firmware_ver[16];

    // WiFi
    bool        wifi_connected;
    int8_t      wifi_rssi;

    // WebSocket
    bool        ws_connected;

    // CSI
    bool        csi_enabled;
    bool        csi_calibrated;
    float       csi_variance;
    float       csi_threshold;
    presence_state_t presence_state;
    float       breath_rate;

    // LED
    light_mode_t light_mode;
    uint16_t    led_duty;
    uint16_t    led_target_duty;

    // Voice
    voice_state_t voice_state;

    // Alarm
    bool        alarm_ringing;
    bool        alarm_snooze_active;

    // System
    uint32_t    uptime_sec;
} system_state_t;

// =============================================================================
// EXTERN DECLARATIONS
// =============================================================================

// Global event group and queues
extern EventGroupHandle_t g_event_group;
extern QueueHandle_t      g_msg_queue;
extern SemaphoreHandle_t  g_state_mutex;
extern system_state_t     g_state;

// NVS namespace for config storage
#define NVS_NAMESPACE       "fairynest"

// =============================================================================
// MODULE FUNCTION DECLARATIONS
// =============================================================================

// wifi_csi.c
esp_err_t wifi_csi_init(void);
void      wifi_csi_task(void *pvParameters);
void      csi_rx_callback(void *ctx, wifi_csi_info_t *data);
float     csi_get_variance(void);
presence_state_t csi_get_presence(void);
esp_err_t csi_set_threshold(float threshold);
esp_err_t csi_start_calibration(void);

// i2s_audio.c
esp_err_t i2s_audio_init(void);
void      i2s_rx_task(void *pvParameters);
void      i2s_tx_task(void *pvParameters);
esp_err_t i2s_audio_start_record(void);
esp_err_t i2s_audio_stop_record(void);
esp_err_t i2s_audio_play(const int16_t *data, size_t samples);
bool      i2s_is_recording(void);
void      i2s_generate_tone(float freq_hz, uint32_t duration_ms);
void      i2s_play_wake_sound(void);
void      i2s_play_alarm_sound(void);
void      i2s_play_beep(int count);

// led_pwm.c
esp_err_t led_pwm_init(void);
void      led_task(void *pvParameters);
esp_err_t led_set_duty(uint16_t duty);
esp_err_t led_set_brightness_percent(uint8_t percent);
esp_err_t led_fade_to(uint16_t target_duty, uint32_t duration_ms);
esp_err_t led_night_on(void);
esp_err_t led_night_off(void);
void      led_start_alarm_fade(uint32_t duration_ms);
void      led_stop_alarm_fade(void);

// websocket_mgr.c
esp_err_t websocket_init(void);
void      websocket_task(void *pvParameters);
esp_err_t websocket_send_json(const char *json_str);
esp_err_t websocket_send_binary(const uint8_t *data, size_t len);
bool      websocket_is_connected(void);

// voice_fsm.c
esp_err_t voice_fsm_init(void);
void      voice_task(void *pvParameters);
esp_err_t voice_handle_wake(void);
esp_err_t voice_handle_command(const char *text);
void      voice_process_text(const char *text);

// alarm_sys.c
esp_err_t alarm_init(void);
void      alarm_task(void *pvParameters);
esp_err_t alarm_set(uint8_t index, bool enabled, uint8_t hour, uint8_t minute,
                    uint8_t days, const char *label);
esp_err_t alarm_stop(void);
esp_err_t alarm_snooze(void);

// config_mgr.c
esp_err_t config_init(void);
esp_err_t config_load(void);
esp_err_t config_save_string(const char *key, const char *value);
esp_err_t config_get_string(const char *key, char *out, size_t out_len, const char *default_val);
esp_err_t config_save_int(const char *key, int32_t value);
esp_err_t config_get_int(const char *key, int32_t *out, int32_t default_val);
esp_err_t config_save_float(const char *key, float value);
esp_err_t config_get_float(const char *key, float *out, float default_val);

#endif // FAIRY_NEST_H
