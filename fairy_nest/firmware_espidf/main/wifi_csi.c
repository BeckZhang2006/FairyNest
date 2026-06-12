/**
 * WiFi CSI Module - ESP-IDF v5.5.2
 *
 * Features:
 * - WiFi STA mode connection with retry logic
 * - CSI (Channel State Information) collection
 * - Subcarrier variance calculation
 * - Presence detection (empty/lying/sitting/moving)
 * - Breath rate estimation
 * - Periodic UDP broadcast to generate WiFi traffic for CSI
 * - Periodic CSI data upload via WebSocket
 */

#include "fairy_nest.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = TAG_WIFI;

// =============================================================================
// INTERNAL STATE
// =============================================================================
static esp_netif_t *s_netif_sta = NULL;
static int s_retry_count = 0;
static float s_csi_variance = 0.0f;
static float s_csi_threshold = CSI_PRESENCE_THRESHOLD_DEFAULT;
static float s_motion_threshold = CSI_MOTION_THRESHOLD_DEFAULT;
static presence_state_t s_presence = PRESENCE_EMPTY;
static uint32_t s_last_presence_ms = 0;
static bool s_calibrating = false;
static bool s_calibrated = false;
static float s_baseline_variance = 0.0f;
static int s_csi_sample_count = 0;
static float s_calibration_sum = 0.0f;

// UDP socket for generating WiFi traffic
static int s_ping_sock = -1;

// =============================================================================
// WIFI EVENT HANDLER
// =============================================================================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started, connecting...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t *evt = (wifi_event_sta_connected_t *)event_data;
                ESP_LOGI(TAG, "WiFi connected to %s", evt->ssid);
                s_retry_count = 0;
                xEventGroupSetBits(g_event_group, EVENT_WIFI_CONNECTED);
                xEventGroupClearBits(g_event_group, EVENT_WIFI_FAIL);

                // Start CSI after WiFi connection
                wifi_csi_config_t csi_config = {
                    .lltf_en = true,
                    .htltf_en = true,
                    .stbc_htltf2_en = true,
                    .ltf_merge_en = true,
                    .channel_filter_en = true,
                    .manu_scale = false,
                    .shift = 15,
                };
                esp_wifi_set_csi_config(&csi_config);
                esp_wifi_set_csi_rx_cb(csi_rx_callback, NULL);
                esp_wifi_set_csi(true);
                ESP_LOGI(TAG, "CSI collection enabled");
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *evt = event_data;
                ESP_LOGW(TAG, "WiFi disconnected, reason: %d", evt->reason);
                xEventGroupClearBits(g_event_group, EVENT_WIFI_CONNECTED);

                if (s_retry_count < WIFI_RETRY_MAX) {
                    ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)...",
                             s_retry_count + 1, WIFI_RETRY_MAX);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    esp_wifi_connect();
                    s_retry_count++;
                } else {
                    ESP_LOGE(TAG, "WiFi connection failed after %d retries", WIFI_RETRY_MAX);
                    xEventGroupSetBits(g_event_group, EVENT_WIFI_FAIL);
                }
                break;
            }

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

            g_state.wifi_connected = true;

            // WiFi connected - play confirmation tone
            i2s_play_wake_sound();
        }
    }
}

// =============================================================================
// UDP PING TRAFFIC GENERATOR
// =============================================================================
static void send_ping_packet(void)
{
    if (s_ping_sock < 0) {
        s_ping_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s_ping_sock < 0) {
            ESP_LOGW(TAG, "Failed to create ping socket");
            return;
        }
        int broadcast = 1;
        setsockopt(s_ping_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    }

    struct sockaddr_in dest_addr = {0};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(12345);
    dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const char *ping_data = "ping";
    int ret = sendto(s_ping_sock, ping_data, strlen(ping_data), 0,
                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (ret < 0) {
        close(s_ping_sock);
        s_ping_sock = -1;
    }
}

// =============================================================================
// WEBSOCKET CSI DATA SENDER
// =============================================================================
static void send_csi_data_ws(float variance, presence_state_t presence)
{
    if (!websocket_is_connected()) {
        return;
    }

    char json_buf[128];
    int len = snprintf(json_buf, sizeof(json_buf),
                       "{\"type\":\"csi_data\",\"data\":[{\"variance\":%.2f,\"presence\":%d}]}",
                       (double)variance, (presence != PRESENCE_EMPTY) ? 1 : 0);

    if (len > 0 && len < (int)sizeof(json_buf)) {
        esp_err_t err = websocket_send_json(json_buf);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send csi_data via WS");
        }
    }
}

// =============================================================================
// CSI CALLBACK (ISR context - keep minimal)
// =============================================================================
void csi_rx_callback(void *ctx, wifi_csi_info_t *data)
{
    (void) ctx;
    if (data == NULL || data->buf == NULL) return;

    // Parse I/Q pairs and calculate amplitude per subcarrier
    int8_t *buf = (int8_t *)data->buf;
    int len = data->len;

    float sum = 0.0f;
    float sum_sq = 0.0f;
    int sc_count = 0;

    for (int i = 0; i < len - 1 && sc_count < CSI_SAMPLE_COUNT; i += 2) {
        int8_t imag = buf[i];
        int8_t real = buf[i + 1];
        float amp_sq = (float)(real * real) + (float)(imag * imag);
        float amp = sqrtf(amp_sq);
        sum += amp;
        sum_sq += amp_sq;
        sc_count++;
    }

    if (sc_count < 2) return;

    // Calculate variance using E[x^2] - (E[x])^2
    float mean = sum / sc_count;
    float variance = (sum_sq / sc_count) - (mean * mean);
    if (variance < 0.0f) variance = 0.0f;

    // Store variance (atomic-ish for single float on 32-bit)
    s_csi_variance = variance;

    // Calibration mode
    if (s_calibrating) {
        s_calibration_sum += variance;
        s_csi_sample_count++;
    }
}

// =============================================================================
// PRESENCE DETECTION LOGIC
// =============================================================================
static presence_state_t detect_presence(float variance)
{
    static int present_count = 0;
    static int empty_count = 0;
    static presence_state_t last_state = PRESENCE_EMPTY;

    // Subtract baseline if calibrated
    float adjusted = variance;
    if (s_calibrated) {
        adjusted = fabsf(variance - s_baseline_variance);
    }

    // Motion check (high variance)
    if (adjusted > s_motion_threshold) {
        present_count++;
        empty_count = 0;
        if (present_count >= 3) {
            last_state = PRESENCE_MOVING;
            return PRESENCE_MOVING;
        }
        return last_state;
    }

    // Presence check (moderate variance)
    if (adjusted > s_csi_threshold) {
        present_count++;
        empty_count = 0;
        if (present_count >= 3) {
            // Distinguish lying vs sitting by variance magnitude
            if (adjusted < s_motion_threshold * 0.6f) {
                last_state = PRESENCE_LYING;
            } else {
                last_state = PRESENCE_SITTING;
            }
        }
        return last_state;
    }

    // Empty
    empty_count++;
    present_count = 0;
    if (empty_count >= 5) {
        last_state = PRESENCE_EMPTY;
        return PRESENCE_EMPTY;
    }
    return last_state;
}

// =============================================================================
// PUBLIC API
// =============================================================================
esp_err_t wifi_csi_init(void)
{
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_netif_sta = esp_netif_create_default_wifi_sta();
    if (s_netif_sta == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // WiFi init
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Configure STA
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_FAIRYNEST_WIFI_SSID,
            .password = CONFIG_FAIRYNEST_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA mode started, SSID: %s", wifi_config.sta.ssid);

    return ESP_OK;
}

void wifi_csi_task(void *pvParameters)
{
    (void) pvParameters;
    ESP_LOGI(TAG, "WiFi+CSI task started on core %d", xPortGetCoreID());

    TickType_t last_wake = xTaskGetTickCount();
    presence_state_t prev_presence = PRESENCE_EMPTY;
    uint32_t last_ping_ms = 0;
    uint32_t last_ws_send_ms = 0;
    float last_valid_variance = 0.0f;

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CSI_SAMPLE_INTERVAL_MS));

        if (!g_state.wifi_connected) {
            continue;
        }

        // Read RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            g_state.wifi_rssi = ap_info.rssi;
        }

        uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Send ping packet periodically to generate WiFi traffic for CSI
        if (now_ms - last_ping_ms >= 100) {
            send_ping_packet();
            last_ping_ms = now_ms;
        }

        // Process CSI
        float variance = s_csi_variance;
        s_csi_variance = 0.0f;  // Reset for next sample

        if (variance < 0.1f) {
            variance = last_valid_variance;  // Use historical value if no new data
        } else {
            last_valid_variance = variance;  // Update history with new valid data
        }

        // Detect presence
        presence_state_t new_presence = detect_presence(variance);
        s_presence = new_presence;

        if (new_presence != PRESENCE_EMPTY) {
            s_last_presence_ms = now_ms;
        }

        // Check for presence timeout
        if (new_presence != PRESENCE_EMPTY &&
            (now_ms - s_last_presence_ms) > CSI_PRESENCE_COOLDOWN_MS) {
            s_presence = PRESENCE_EMPTY;
            new_presence = PRESENCE_EMPTY;
        }

        // Update global state
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_state.csi_variance = variance;
            g_state.presence_state = s_presence;
            xSemaphoreGive(g_state_mutex);
        }

        // Send csi_data via WebSocket periodically (every 2 seconds)
        if (now_ms - last_ws_send_ms >= 2000) {
            last_ws_send_ms = now_ms;
            send_csi_data_ws(variance, new_presence);
        }

        // Presence changed
        if (new_presence != prev_presence) {
            fairy_msg_t msg = {
                .type = MSG_CSI_PRESENCE_CHANGED,
                .param_i = (int32_t)new_presence,
            };
            xQueueSend(g_msg_queue, &msg, 0);

            ESP_LOGI(TAG, "Presence: %s -> %s (variance: %.1f)",
                     prev_presence == PRESENCE_EMPTY ? "empty" :
                     prev_presence == PRESENCE_LYING ? "lying" :
                     prev_presence == PRESENCE_SITTING ? "sitting" : "moving",
                     new_presence == PRESENCE_EMPTY ? "empty" :
                     new_presence == PRESENCE_LYING ? "lying" :
                     new_presence == PRESENCE_SITTING ? "sitting" : "moving",
                     variance);

            prev_presence = new_presence;
        }
    }
}

float csi_get_variance(void)
{
    return s_csi_variance;
}

presence_state_t csi_get_presence(void)
{
    return s_presence;
}

esp_err_t csi_set_threshold(float threshold)
{
    if (threshold < 1.0f || threshold > 100.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    s_csi_threshold = threshold;
    s_motion_threshold = threshold * 1.67f;  // Motion threshold = 1.67x presence

    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_state.csi_threshold = threshold;
        xSemaphoreGive(g_state_mutex);
    }

    ESP_LOGI(TAG, "CSI threshold set to: %.1f (motion: %.1f)",
             s_csi_threshold, s_motion_threshold);

    // Persist to NVS
    config_save_float("csi_threshold", threshold);
    return ESP_OK;
}

esp_err_t csi_start_calibration(void)
{
    ESP_LOGI(TAG, "Starting CSI calibration...");
    s_calibrating = true;
    s_calibrated = false;
    s_calibration_sum = 0.0f;
    s_csi_sample_count = 0;

    // Wait for calibration duration
    vTaskDelay(pdMS_TO_TICKS(CSI_CALIBRATION_DURATION_MS));

    if (s_csi_sample_count > 0) {
        s_baseline_variance = s_calibration_sum / s_csi_sample_count;
        s_calibrated = true;
        ESP_LOGI(TAG, "Calibration complete: baseline variance = %.2f (%d samples)",
                 s_baseline_variance, s_csi_sample_count);

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_state.csi_calibrated = true;
            xSemaphoreGive(g_state_mutex);
        }
    } else {
        ESP_LOGW(TAG, "Calibration failed: no CSI samples collected");
    }

    s_calibrating = false;
    xEventGroupSetBits(g_event_group, EVENT_CSI_CALIBRATED);
    return s_calibrated ? ESP_OK : ESP_FAIL;
}
