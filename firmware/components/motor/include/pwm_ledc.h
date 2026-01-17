/**
 * @file pwm_ledc.h
 * @brief ESP32 LEDC PWM driver
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief PWM channel configuration
 */
typedef struct {
    uint8_t gpio_num;        ///< GPIO pin number
    uint8_t ledc_channel;    ///< LEDC channel (0-7)
    uint32_t freq_hz;        ///< PWM frequency in Hz
    uint8_t resolution;      ///< PWM resolution in bits (8-14)
} pwm_ledc_config_t;

/**
 * @brief Initialize a PWM channel
 * 
 * @param config PWM configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pwm_ledc_init(const pwm_ledc_config_t *config);

/**
 * @brief Set PWM duty cycle
 * 
 * @param channel LEDC channel
 * @param duty Duty cycle (0 to 2^resolution - 1)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pwm_ledc_set_duty(uint8_t channel, uint32_t duty);

/**
 * @brief Get maximum duty value for current resolution
 * 
 * @param resolution PWM resolution in bits
 * @return uint32_t Maximum duty value
 */
static inline uint32_t pwm_ledc_get_max_duty(uint8_t resolution) {
    return (1 << resolution) - 1;
}
