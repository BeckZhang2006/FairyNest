/**
 * Configuration Manager - ESP-IDF v5.5.2
 *
 * Persistent storage using NVS (Non-Volatile Storage).
 * Stores WiFi credentials, alarm settings, CSI thresholds, and device config.
 */

#include "fairy_nest.h"

static const char *TAG = TAG_CFG;

// =============================================================================
// INTERNAL STATE
// =============================================================================
static nvs_handle_t s_nvs_handle = 0;
static bool s_initialized = false;

// =============================================================================
// INITIALIZATION
// =============================================================================
esp_err_t config_init(void)
{
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Configuration manager initialized");
    return ESP_OK;
}

esp_err_t config_load(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Load CSI threshold
    int32_t csi_thresh = 0;
    config_get_int("csi_threshold", &csi_thresh, CSI_PRESENCE_THRESHOLD_DEFAULT);
    g_state.csi_threshold = (float)csi_thresh;

    ESP_LOGI(TAG, "Configuration loaded from NVS");
    return ESP_OK;
}

// =============================================================================
// STRING
// =============================================================================
esp_err_t config_save_string(const char *key, const char *value)
{
    if (!s_initialized || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = nvs_set_str(s_nvs_handle, key, value);
    if (ret == ESP_OK) {
        nvs_commit(s_nvs_handle);
    }
    return ret;
}

esp_err_t config_get_string(const char *key, char *out, size_t out_len,
                            const char *default_val)
{
    if (!s_initialized || key == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t len = out_len;
    esp_err_t ret = nvs_get_str(s_nvs_handle, key, out, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND && default_val) {
        strncpy(out, default_val, out_len - 1);
        out[out_len - 1] = '\0';
        return ESP_OK;
    }
    return ret;
}

// =============================================================================
// INTEGER
// =============================================================================
esp_err_t config_save_int(const char *key, int32_t value)
{
    if (!s_initialized || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = nvs_set_i32(s_nvs_handle, key, value);
    if (ret == ESP_OK) {
        nvs_commit(s_nvs_handle);
    }
    return ret;
}

esp_err_t config_get_int(const char *key, int32_t *out, int32_t default_val)
{
    if (!s_initialized || key == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_get_i32(s_nvs_handle, key, out);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *out = default_val;
        return ESP_OK;
    }
    return ret;
}

// =============================================================================
// FLOAT
// =============================================================================
esp_err_t config_save_float(const char *key, float value)
{
    // Store float as integer (multiply by 1000 for 3 decimal precision)
    int32_t scaled = (int32_t)(value * 1000.0f);
    return config_save_int(key, scaled);
}

esp_err_t config_get_float(const char *key, float *out, float default_val)
{
    int32_t scaled = 0;
    esp_err_t ret = config_get_int(key, &scaled, (int32_t)(default_val * 1000.0f));
    if (ret == ESP_OK) {
        *out = (float)scaled / 1000.0f;
    }
    return ret;
}
