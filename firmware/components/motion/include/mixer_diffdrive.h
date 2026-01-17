/**
 * @file mixer_diffdrive.h
 * @brief Differential drive mixer (throttle + steering -> left/right speeds)
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Mixer configuration
 */
typedef struct {
    float deadzone;         ///< Deadzone (0.0 to 0.2, e.g., 0.05 = 5%)
    float expo;             ///< Expo curve (0.0 to 1.0, e.g., 0.3 = 30%)
    float max_speed;        ///< Max speed limit (0.0 to 1.0)
    float slow_mode_factor; ///< Slow mode multiplier (0.0 to 1.0)
} mixer_config_t;

/**
 * @brief Initialize differential drive mixer
 * 
 * @param config Mixer configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mixer_diffdrive_init(const mixer_config_t *config);

/**
 * @brief Mix throttle and steering into left/right motor speeds
 * 
 * @param throttle Throttle input (-1.0 to +1.0)
 * @param steering Steering input (-1.0 to +1.0)
 * @param slow_mode Enable slow mode
 * @param left_out Output: left motor speed
 * @param right_out Output: right motor speed
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mixer_diffdrive_mix(float throttle, float steering, bool slow_mode,
                               float *left_out, float *right_out);
