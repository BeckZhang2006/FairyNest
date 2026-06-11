/**
 * LED PWM Controller - ESP-IDF v5.5.2
 *
 * Features:
 * - LEDC hardware PWM with 12-bit resolution
 * - Smooth brightness fading (software implementation)
 * - Night light mode (low brightness)
 * - Alarm sunrise simulation (gradual brightness increase)
 * - Breathing effect
 * - Gamma correction for perceptual linearity
 * - Presence-responsive auto-control
 *
 * Uses LEDC low-speed mode (ESP32-S3 has no high-speed mode)
 */

#include "fairy_nest.h"

static const char *TAG = TAG_LED;

// =============================================================================
// INTERNAL STATE
// =============================================================================
static uint16_t s_current_duty = 0;
static uint16_t s_target_duty = 0;
static uint16_t s_start_duty = 0;
static uint32_t s_fade_start_ms = 0;
static uint32_t s_fade_duration_ms = 0;
static bool s_fading = false;
static bool s_alarm_fading = false;
static uint32_t s_alarm_start_ms = 0;
static uint32_t s_alarm_duration_ms = 0;
static bool s_breathing = false;
static uint32_t s_breath_period_ms = LED_BREATH_PERIOD_MS;
static float s_gamma = 2.2f;

// =============================================================================
// GAMMA CORRECTION
// =============================================================================
static uint16_t gamma_correct(uint16_t duty)
{
    float normalized = (float)duty / LED_PWM_MAX_DUTY;
    float corrected = powf(normalized, s_gamma);
    return (uint16_t)(corrected * LED_PWM_MAX_DUTY);
}

// =============================================================================
// WRITE DUTY TO HARDWARE
// =============================================================================
static esp_err_t write_duty(uint16_t duty)
{
    uint16_t corrected = gamma_correct(duty);
    esp_err_t ret = ledc_set_duty(LED_PWM_MODE, LED_PWM_CHANNEL, corrected);
    if (ret != ESP_OK) {
        return ret;
    }
    return ledc_update_duty(LED_PWM_MODE, LED_PWM_CHANNEL);
}

// =============================================================================
// INITIALIZATION
// =============================================================================
esp_err_t led_pwm_init(void)
{
    ESP_LOGI(TAG, "Initializing LED PWM (GPIO%d, %dHz, %d-bit)",
             LED_GPIO_PIN, LED_PWM_FREQ_HZ, LED_PWM_RESOLUTION);

    // Clear any previous state
    ledc_stop(LED_PWM_MODE, LED_PWM_CHANNEL, 0);

    // Timer configuration
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LED_PWM_MODE,
        .duty_resolution = LED_PWM_RESOLUTION,
        .timer_num = LED_PWM_TIMER,
        .freq_hz = LED_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };

    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Channel configuration
    ledc_channel_config_t channel_cfg = {
        .gpio_num = LED_GPIO_PIN,
        .speed_mode = LED_PWM_MODE,
        .channel = LED_PWM_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LED_PWM_TIMER,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    };

    ret = ledc_channel_config(&channel_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start with LED off
    s_current_duty = 0;
    s_target_duty = 0;
    write_duty(0);

    // Set initial mode
    g_state.light_mode = LIGHT_MODE_OFF;
    g_state.led_duty = 0;

    ESP_LOGI(TAG, "LED PWM initialized OK");
    return ESP_OK;
}

// =============================================================================
// LED TASK - Main PWM control loop
// =============================================================================
void led_task(void *pvParameters)
{
    (void) pvParameters;
    ESP_LOGI(TAG, "LED task started on core %d", xPortGetCoreID());

    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(20));  // 50 Hz update rate

        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // ---- Handle fade ----
        if (s_fading) {
            if (now_ms >= s_fade_start_ms + s_fade_duration_ms) {
                // Fade complete
                s_current_duty = s_target_duty;
                s_fading = false;
            } else {
                float progress = (float)(now_ms - s_fade_start_ms) / s_fade_duration_ms;
                // Smoothstep easing
                progress = progress * progress * (3.0f - 2.0f * progress);
                s_current_duty = (uint16_t)(s_start_duty +
                    (s_target_duty - s_start_duty) * progress);
            }
            write_duty(s_current_duty);
        }

        // ---- Handle alarm fade ----
        if (s_alarm_fading) {
            uint32_t elapsed = now_ms - s_alarm_start_ms;
            if (elapsed >= s_alarm_duration_ms) {
                s_alarm_fading = false;
                s_current_duty = LED_PWM_MAX_DUTY;
                write_duty(LED_PWM_MAX_DUTY);
                ESP_LOGI(TAG, "Alarm fade complete");
            } else {
                // Smoothstep for natural sunrise
                float t = (float)elapsed / s_alarm_duration_ms;
                t = t * t * (3.0f - 2.0f * t);
                uint16_t duty = (uint16_t)(t * LED_PWM_MAX_DUTY);
                s_current_duty = duty;
                write_duty(duty);
            }
        }

        // ---- Handle breathing ----
        if (s_breathing && !s_alarm_fading && !s_fading) {
            float phase = (float)(now_ms % s_breath_period_ms) / s_breath_period_ms;
            float sine = (sinf(2.0f * M_PI * phase - M_PI_2) + 1.0f) / 2.0f;
            sine = sine * sine;  // Ease-in
            uint16_t min_duty = 20;
            uint16_t max_duty = 300;
            uint16_t duty = (uint16_t)(min_duty + (max_duty - min_duty) * sine);
            s_current_duty = duty;
            write_duty(duty);
        }

        // ---- Check message queue ----
        fairy_msg_t msg;
        if (xQueueReceive(g_msg_queue, &msg, 0) == pdTRUE) {
            switch (msg.type) {
                case MSG_LED_ON:
                    ESP_LOGI(TAG, "LED ON command");
                    s_target_duty = LED_PWM_MAX_DUTY;
                    s_start_duty = s_current_duty;
                    s_fade_start_ms = now_ms;
                    s_fade_duration_ms = LED_FADE_MS_DEFAULT;
                    s_fading = true;
                    s_breathing = false;
                    s_alarm_fading = false;
                    g_state.light_mode = LIGHT_MODE_MANUAL;
                    break;

                case MSG_LED_OFF:
                    ESP_LOGI(TAG, "LED OFF command");
                    s_target_duty = 0;
                    s_start_duty = s_current_duty;
                    s_fade_start_ms = now_ms;
                    s_fade_duration_ms = LED_FADE_MS_DEFAULT;
                    s_fading = true;
                    s_breathing = false;
                    s_alarm_fading = false;
                    g_state.light_mode = LIGHT_MODE_OFF;
                    break;

                case MSG_LED_SET_BRIGHTNESS: {
                    uint8_t percent = (uint8_t)msg.param_i;
                    uint16_t duty = (uint16_t)((percent * LED_PWM_MAX_DUTY) / 100);
                    s_target_duty = duty;
                    s_start_duty = s_current_duty;
                    s_fade_start_ms = now_ms;
                    s_fade_duration_ms = 1000;
                    s_fading = true;
                    s_breathing = false;
                    s_alarm_fading = false;
                    g_state.light_mode = LIGHT_MODE_MANUAL;
                    ESP_LOGI(TAG, "LED brightness set to %d%%", percent);
                    break;
                }

                case MSG_LED_NIGHT_MODE:
                    ESP_LOGI(TAG, "LED night mode");
                    s_target_duty = LED_NIGHT_BRIGHTNESS_DEFAULT;
                    s_start_duty = s_current_duty;
                    s_fade_start_ms = now_ms;
                    s_fade_duration_ms = LED_FADE_MS_DEFAULT;
                    s_fading = true;
                    s_breathing = false;
                    s_alarm_fading = false;
                    g_state.light_mode = LIGHT_MODE_NIGHT;
                    break;

                case MSG_LED_ALARM_FADE_START:
                    ESP_LOGI(TAG, "LED alarm fade start");
                    s_alarm_start_ms = now_ms;
                    s_alarm_duration_ms = ALARM_FADE_MS_DEFAULT;
                    s_alarm_fading = true;
                    s_fading = false;
                    s_breathing = false;
                    g_state.light_mode = LIGHT_MODE_ALARM_FADE;
                    break;

                case MSG_LED_ALARM_FADE_STOP:
                    ESP_LOGI(TAG, "LED alarm fade stop");
                    s_alarm_fading = false;
                    s_target_duty = 0;
                    s_start_duty = s_current_duty;
                    s_fade_start_ms = now_ms;
                    s_fade_duration_ms = LED_FADE_MS_DEFAULT;
                    s_fading = true;
                    g_state.light_mode = LIGHT_MODE_OFF;
                    break;

                case MSG_CSI_PRESENCE_CHANGED: {
                    presence_state_t ps = (presence_state_t)msg.param_i;
                    // Auto night light on presence at night
                    int hour = 0;
                    time_t now = time(NULL);
                    struct tm *tm_info = localtime(&now);
                    if (tm_info) hour = tm_info->tm_hour;

                    bool is_night = (hour >= 22 || hour <= 6);

                    if (ps != PRESENCE_EMPTY && is_night &&
                        g_state.light_mode == LIGHT_MODE_OFF) {
                        ESP_LOGI(TAG, "Auto night light ON (presence at night)");
                        fairy_msg_t led_msg = {.type = MSG_LED_NIGHT_MODE};
                        xQueueSend(g_msg_queue, &led_msg, 0);
                    } else if (ps == PRESENCE_EMPTY &&
                               g_state.light_mode == LIGHT_MODE_NIGHT) {
                        ESP_LOGI(TAG, "Auto night light OFF (no presence)");
                        fairy_msg_t led_msg = {.type = MSG_LED_OFF};
                        xQueueSend(g_msg_queue, &led_msg, 0);
                    }
                    break;
                }

                default:
                    break;
            }
        }

        // Update global state
        g_state.led_duty = s_current_duty;
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================
esp_err_t led_set_duty(uint16_t duty)
{
    s_current_duty = duty;
    s_fading = false;
    s_alarm_fading = false;
    s_breathing = false;
    return write_duty(duty);
}

esp_err_t led_set_brightness_percent(uint8_t percent)
{
    uint16_t duty = (uint16_t)((percent * LED_PWM_MAX_DUTY) / 100);
    return led_set_duty(duty);
}

esp_err_t led_fade_to(uint16_t target_duty, uint32_t duration_ms)
{
    s_target_duty = target_duty;
    s_start_duty = s_current_duty;
    s_fade_start_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    s_fade_duration_ms = duration_ms;
    s_fading = true;
    s_alarm_fading = false;
    return ESP_OK;
}

esp_err_t led_night_on(void)
{
    fairy_msg_t msg = {.type = MSG_LED_NIGHT_MODE};
    xQueueSend(g_msg_queue, &msg, portMAX_DELAY);
    return ESP_OK;
}

esp_err_t led_night_off(void)
{
    fairy_msg_t msg = {.type = MSG_LED_OFF};
    xQueueSend(g_msg_queue, &msg, portMAX_DELAY);
    return ESP_OK;
}

void led_start_alarm_fade(uint32_t duration_ms)
{
    fairy_msg_t msg = {
        .type = MSG_LED_ALARM_FADE_START,
        .param_i = (int32_t)duration_ms,
    };
    xQueueSend(g_msg_queue, &msg, portMAX_DELAY);
}

void led_stop_alarm_fade(void)
{
    fairy_msg_t msg = {.type = MSG_LED_ALARM_FADE_STOP};
    xQueueSend(g_msg_queue, &msg, portMAX_DELAY);
}
