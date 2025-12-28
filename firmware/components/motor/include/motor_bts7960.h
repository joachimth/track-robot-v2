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
 * @brief Emergency stop (immediate, no ramping)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t motor_emergency_stop(void);
