/**
 * @file mixer_diffdrive.c
 * @brief Differential drive mixer implementation
 */

#include "mixer_diffdrive.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "mixer";

static mixer_config_t mixer_cfg;

/**
 * @brief Apply deadzone
 */
static float apply_deadzone(float value, float deadzone) {
    if (fabsf(value) < deadzone) {
        return 0.0f;
    }
    // Scale remaining range
    float sign = (value > 0.0f) ? 1.0f : -1.0f;
    return sign * (fabsf(value) - deadzone) / (1.0f - deadzone);
}

/**
 * @brief Apply expo curve
 * 
 * Expo gives finer control near center, more aggressive at extremes.
 * Formula: output = expo * value^3 + (1 - expo) * value
 */
static float apply_expo(float value, float expo) {
    float cubic = value * value * value;
    return expo * cubic + (1.0f - expo) * value;
}

/**
 * @brief Clamp value to [-1.0, +1.0]
 */
static float clamp(float value) {
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}

esp_err_t mixer_diffdrive_init(const mixer_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "NULL config");
        return ESP_ERR_INVALID_ARG;
    }
    
    mixer_cfg = *config;
    
    ESP_LOGI(TAG, "Differential drive mixer initialized");
    ESP_LOGI(TAG, "  Deadzone: %.1f%%", mixer_cfg.deadzone * 100.0f);
    ESP_LOGI(TAG, "  Expo: %.1f%%", mixer_cfg.expo * 100.0f);
    ESP_LOGI(TAG, "  Max speed: %.1f%%", mixer_cfg.max_speed * 100.0f);
    ESP_LOGI(TAG, "  Slow mode factor: %.1f%%", mixer_cfg.slow_mode_factor * 100.0f);
    
    return ESP_OK;
}

esp_err_t mixer_diffdrive_mix(float throttle, float steering, bool slow_mode,
                               float *left_out, float *right_out) {
    if (left_out == NULL || right_out == NULL) {
        ESP_LOGE(TAG, "NULL output pointers");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Apply deadzone
    throttle = apply_deadzone(throttle, mixer_cfg.deadzone);
    steering = apply_deadzone(steering, mixer_cfg.deadzone);
    
    // Apply expo
    throttle = apply_expo(throttle, mixer_cfg.expo);
    steering = apply_expo(steering, mixer_cfg.expo);
    
    // Differential drive mixing
    // Left = throttle + steering
    // Right = throttle - steering
    float left = throttle + steering;
    float right = throttle - steering;
    
    // Clamp to [-1.0, +1.0]
    left = clamp(left);
    right = clamp(right);
    
    // Apply max speed limit
    left *= mixer_cfg.max_speed;
    right *= mixer_cfg.max_speed;
    
    // Apply slow mode
    if (slow_mode) {
        left *= mixer_cfg.slow_mode_factor;
        right *= mixer_cfg.slow_mode_factor;
    }
    
    *left_out = left;
    *right_out = right;
    
    return ESP_OK;
}
