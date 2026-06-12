/**
 * I2S Audio Module - ESP-IDF v5.5.2
 *
 * Features:
 * - I2S microphone input (INMP441)
 * - I2S speaker output (MAX98357A)
 * - Audio recording with circular buffer
 * - Tone generation for feedback sounds
 * - TTS audio playback
 *
 * Uses ESP-IDF legacy I2S API for maximum compatibility.
 */

#include "fairy_nest.h"

static const char *TAG = TAG_I2S;

// =============================================================================
// INTERNAL STATE
// =============================================================================
static bool s_recording = false;
static SemaphoreHandle_t s_tx_semaphore = NULL;
static SemaphoreHandle_t s_record_sem = NULL;

// TX tone buffer
static int16_t s_tone_buffer[AUDIO_BUFFER_SAMPLES];
static size_t s_tone_samples = 0;
static volatile bool s_playing_tone = false;

// PCM playback queue for TTS audio
#define PCM_QUEUE_SIZE          16
#define PCM_MAX_PACKET_SIZE     2048

typedef struct {
    uint8_t *data;
    size_t len;
} pcm_packet_t;

static QueueHandle_t s_pcm_queue = NULL;
static uint32_t s_last_wake_ms = 0;
static uint32_t s_silence_frames = 0;

// =============================================================================
// I2S INITIALIZATION
// =============================================================================
esp_err_t i2s_audio_init(void)
{
    esp_err_t ret;

    s_tx_semaphore = xSemaphoreCreateBinary();
    s_record_sem = xSemaphoreCreateBinary();
    s_pcm_queue = xQueueCreate(PCM_QUEUE_SIZE, sizeof(pcm_packet_t));

    // ---- Microphone I2S (RX) ----
    i2s_config_t i2s_mic_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = AUDIO_SAMPLE_BITS,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = AUDIO_DMA_BUF_COUNT,
        .dma_buf_len = AUDIO_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };

    i2s_pin_config_t mic_pins = {
        .bck_io_num = I2S_MIC_GPIO_SCK,
        .ws_io_num = I2S_MIC_GPIO_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_MIC_GPIO_SD,
    };

    ret = i2s_driver_install(I2S_MIC_PORT, &i2s_mic_cfg, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mic I2S driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_set_pin(I2S_MIC_PORT, &mic_pins);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mic I2S pin config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2S microphone initialized on port %d", I2S_MIC_PORT);

    // ---- Speaker I2S (TX) ----
    i2s_config_t i2s_spk_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = AUDIO_SAMPLE_BITS,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = AUDIO_DMA_BUF_COUNT,
        .dma_buf_len = AUDIO_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    i2s_pin_config_t spk_pins = {
        .bck_io_num = I2S_SPK_GPIO_BCLK,
        .ws_io_num = I2S_SPK_GPIO_LRC,
        .data_out_num = I2S_SPK_GPIO_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    ret = i2s_driver_install(I2S_SPK_PORT, &i2s_spk_cfg, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Speaker I2S driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_set_pin(I2S_SPK_PORT, &spk_pins);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Speaker I2S pin config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2S speaker initialized on port %d", I2S_SPK_PORT);

    return ESP_OK;
}

// =============================================================================
// I2S RX TASK - Microphone recording
// =============================================================================
void i2s_rx_task(void *pvParameters)
{
    (void) pvParameters;
    ESP_LOGI(TAG, "I2S RX task started on core %d", xPortGetCoreID());

    int16_t rx_buffer[AUDIO_BUFFER_SAMPLES];

    for (;;) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_read(I2S_MIC_PORT, rx_buffer, AUDIO_BUFFER_SIZE,
                                  &bytes_read, portMAX_DELAY);
        if (ret != ESP_OK || bytes_read == 0) {
            continue;
        }

        int samples_read = bytes_read / sizeof(int16_t);

        // If recording, send data via WebSocket
        if (s_recording) {
            if (g_state.ws_connected) {
                websocket_send_binary((const uint8_t *)rx_buffer, bytes_read);
            }
        }

        // Voice Activity Detection (VAD) - energy-based
        int32_t energy = 0;
        for (int i = 0; i < samples_read; i++) {
            energy += abs(rx_buffer[i]);
        }
        energy /= samples_read;

        if (s_recording) {
            // Recording VAD: detect silence to auto-stop
            if (energy < VAD_ENERGY_THRESHOLD) {
                s_silence_frames++;
                // Each buffer is ~32ms (512/16000)
                uint32_t silence_ms = s_silence_frames * AUDIO_BUFFER_SAMPLES * 1000 / AUDIO_SAMPLE_RATE;
                if (silence_ms > VAD_SILENCE_TIMEOUT_MS) {
                    s_silence_frames = 0;
                    fairy_msg_t msg = {.type = MSG_VOICE_VAD_SILENCE};
                    xQueueSend(g_msg_queue, &msg, 0);
                }
            } else {
                s_silence_frames = 0;
            }
        } else {
            // Wake word detection with cooldown to reduce false triggers
            uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (now_ms - s_last_wake_ms > 3000) {
                static int trigger_count = 0;
                if (energy > VAD_ENERGY_THRESHOLD * WAKE_ENERGY_MULTIPLIER) {
                    trigger_count++;
                    if (trigger_count > 10) {  // ~320ms sustained energy
                        trigger_count = 0;
                        s_last_wake_ms = now_ms;
                        fairy_msg_t msg = {.type = MSG_VOICE_WAKE_DETECTED};
                        xQueueSend(g_msg_queue, &msg, 0);
                    }
                } else {
                    trigger_count = 0;
                }
            }
        }
    }
}

// =============================================================================
// I2S TX TASK - Speaker playback
// =============================================================================
void i2s_tx_task(void *pvParameters)
{
    (void) pvParameters;
    ESP_LOGI(TAG, "I2S TX task started on core %d", xPortGetCoreID());

    for (;;) {
        // Wait for playback request
        if (xSemaphoreTake(s_tx_semaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (s_playing_tone && s_tone_samples > 0) {
                // Play tone buffer
                size_t bytes_written = 0;
                size_t bytes_to_write = s_tone_samples * sizeof(int16_t);

                while (bytes_written < bytes_to_write) {
                    size_t chunk = 0;
                    esp_err_t ret = i2s_write(I2S_SPK_PORT,
                                              (const uint8_t *)s_tone_buffer + bytes_written,
                                              bytes_to_write - bytes_written,
                                              &chunk, pdMS_TO_TICKS(100));
                    if (ret == ESP_OK) {
                        bytes_written += chunk;
                    } else {
                        break;
                    }
                }
                s_playing_tone = false;
                s_tone_samples = 0;
            }

            // Play queued PCM packets (TTS audio from server)
            pcm_packet_t pkt;
            while (xQueueReceive(s_pcm_queue, &pkt, 0) == pdTRUE) {
                size_t bytes_written = 0;
                while (bytes_written < pkt.len) {
                    size_t chunk = 0;
                    esp_err_t ret = i2s_write(I2S_SPK_PORT,
                                              pkt.data + bytes_written,
                                              pkt.len - bytes_written,
                                              &chunk, pdMS_TO_TICKS(200));
                    if (ret == ESP_OK) {
                        bytes_written += chunk;
                    } else {
                        ESP_LOGE(TAG, "PCM write error: %s", esp_err_to_name(ret));
                        break;
                    }
                }
                free(pkt.data);
            }
        }
    }
}

// =============================================================================
// RECORDING CONTROL
// =============================================================================
esp_err_t i2s_audio_start_record(void)
{
    s_recording = true;
    s_silence_frames = 0;
    g_state.voice_state = VOICE_STATE_RECORDING;
    ESP_LOGI(TAG, "Audio recording started");
    return ESP_OK;
}

esp_err_t i2s_audio_stop_record(void)
{
    s_recording = false;
    g_state.voice_state = VOICE_STATE_PROCESSING;
    ESP_LOGI(TAG, "Audio recording stopped");
    return ESP_OK;
}

bool i2s_is_recording(void)
{
    return s_recording;
}

// =============================================================================
// AUDIO PLAYBACK
// =============================================================================
esp_err_t i2s_audio_play(const int16_t *data, size_t samples)
{
    if (data == NULL || samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t bytes_written = 0;
    size_t total_bytes = samples * sizeof(int16_t);

    while (bytes_written < total_bytes) {
        size_t chunk = 0;
        esp_err_t ret = i2s_write(I2S_SPK_PORT,
                                  (const uint8_t *)data + bytes_written,
                                  total_bytes - bytes_written,
                                  &chunk, pdMS_TO_TICKS(500));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(ret));
            return ret;
        }
        bytes_written += chunk;
    }

    return ESP_OK;
}

esp_err_t i2s_audio_play_pcm(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_pcm_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    pcm_packet_t pkt;
    pkt.len = len;
    pkt.data = (uint8_t *)malloc(len);
    if (pkt.data == NULL) {
        ESP_LOGE(TAG, "Failed to alloc PCM packet (%d bytes)", (int)len);
        return ESP_ERR_NO_MEM;
    }

    memcpy(pkt.data, data, len);

    if (xQueueSend(s_pcm_queue, &pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "PCM queue full, dropping packet");
        free(pkt.data);
        return ESP_FAIL;
    }

    xSemaphoreGive(s_tx_semaphore);
    return ESP_OK;
}

// =============================================================================
// TONE GENERATION
// =============================================================================
void i2s_generate_tone(float freq_hz, uint32_t duration_ms)
{
    int samples = (AUDIO_SAMPLE_RATE * duration_ms) / 1000;
    if (samples > AUDIO_BUFFER_SAMPLES) {
        samples = AUDIO_BUFFER_SAMPLES;
    }

    // Generate sine wave at 50% volume
    float volume = 0.5f;
    for (int i = 0; i < samples; i++) {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        s_tone_buffer[i] = (int16_t)(32767.0f * volume *
                                     sinf(2.0f * M_PI * freq_hz * t));
    }

    s_tone_samples = samples;
    s_playing_tone = true;
    xSemaphoreGive(s_tx_semaphore);
}

void i2s_play_wake_sound(void)
{
    // Friendly ascending chime: A5 -> C#6 -> E6
    i2s_generate_tone(880.0f, 80);   // A5
    vTaskDelay(pdMS_TO_TICKS(50));
    i2s_generate_tone(1100.0f, 100); // C#6
    vTaskDelay(pdMS_TO_TICKS(50));
    i2s_generate_tone(1320.0f, 150); // E6
}

void i2s_play_alarm_sound(void)
{
    // Rising alarm pattern
    for (int i = 0; i < 3; i++) {
        i2s_generate_tone(800.0f + i * 200, 150);
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

void i2s_play_beep(int count)
{
    for (int i = 0; i < count; i++) {
        i2s_generate_tone(1000.0f, 80);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}
