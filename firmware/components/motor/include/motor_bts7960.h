/**
 * @file motor_bts7960.h
 * @brief BTS7960 dual H-bridge motor driver
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Motor configuration
 */
typedef struct {
    // Left motor pins
    uint8_t left_rpwm;
    uint8_t left_lpwm;
    uint8_t left_ren;
    uint8_t left_len;
    
    // Right motor pins
    uint8_t right_rpwm;
    uint8_t right_lpwm;
    uint8_t right_ren;
    uint8_t right_len;
    
    // PWM settings
    uint32_t pwm_freq_hz;
    uint8_t pwm_resolution;
    uint32_t ramp_rate_ms;
    
    // Motor inversion
    bool invert_left;
    bool invert_right;
} motor_config_t;

/**
 * @brief Initialize motor driver
 * 
 * @param config Motor configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t motor_bts7960_init(const motor_config_t *config);

/**
 * @brief Set motor speeds
 * 
 * @param left_speed Left motor speed (-1.0 to +1.0)
 * @param right_speed Right motor speed (-1.0 to +1.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t motor_set_speeds(float left_speed, float right_speed);

/**
 * @brief Get current motor speeds
 *
 * @param left_target  Target left speed (set by control loop)
 * @param right_target Target right speed (set by control loop)
 * @param left_actual  Actual left speed (after slew-rate ramp)
 * @param right_actual Actual right speed (after slew-rate ramp)
 */
void motor_get_speeds(float *left_target, float *right_target,
                      float *left_actual, float *right_actual);

/**
 * @brief Emergency stop (immediate, bypasses slew-rate ramp)
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t motor_emergency_stop(void);
