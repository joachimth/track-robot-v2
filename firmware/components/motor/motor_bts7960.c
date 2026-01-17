/**
 * @file motor_bts7960.c
 * @brief BTS7960 motor driver implementation
 */

#include "motor_bts7960.h"
#include "pwm_ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "motor_bts7960";

// Motor state
static motor_config_t motor_cfg;
static uint32_t max_duty;
static float current_left_speed = 0.0f;
static float current_right_speed = 0.0f;
static float target_left_speed = 0.0f;
static float target_right_speed = 0.0f;

// LEDC channels
#define CH_LEFT_RPWM 0
#define CH_LEFT_LPWM 1
#define CH_RIGHT_RPWM 2
#define CH_RIGHT_LPWM 3

/**
 * @brief Apply motor speed to hardware
 */
static void apply_motor_speed(float left, float right, bool inverted_left, bool inverted_right) {
    if (inverted_left) left = -left;
    if (inverted_right) right = -right;
    
    // Left motor
    uint32_t left_duty = (uint32_t)(fabsf(left) * max_duty);
    if (left >= 0.0f) {
        pwm_ledc_set_duty(CH_LEFT_LPWM, left_duty);
        pwm_ledc_set_duty(CH_LEFT_RPWM, 0);
    } else {
        pwm_ledc_set_duty(CH_LEFT_LPWM, 0);
        pwm_ledc_set_duty(CH_LEFT_RPWM, left_duty);
    }
    
    // Right motor
    uint32_t right_duty = (uint32_t)(fabsf(right) * max_duty);
    if (right >= 0.0f) {
        pwm_ledc_set_duty(CH_RIGHT_LPWM, right_duty);
        pwm_ledc_set_duty(CH_RIGHT_RPWM, 0);
    } else {
        pwm_ledc_set_duty(CH_RIGHT_LPWM, 0);
        pwm_ledc_set_duty(CH_RIGHT_RPWM, right_duty);
    }
}

/**
 * @brief Motor ramping task
 */
static void motor_ramp_task(void *arg) {
    const uint32_t loop_rate_ms = 20; // 50Hz
    
    while (1) {
        if (motor_cfg.ramp_rate_ms > 0) {
            // Calculate ramp step per loop iteration
            float max_change = (1.0f / motor_cfg.ramp_rate_ms) * loop_rate_ms;
            
            // Ramp left motor
            float left_diff = target_left_speed - current_left_speed;
            if (fabsf(left_diff) > max_change) {
                current_left_speed += (left_diff > 0.0f) ? max_change : -max_change;
            } else {
                current_left_speed = target_left_speed;
            }
            
            // Ramp right motor
            float right_diff = target_right_speed - current_right_speed;
            if (fabsf(right_diff) > max_change) {
                current_right_speed += (right_diff > 0.0f) ? max_change : -max_change;
            } else {
                current_right_speed = target_right_speed;
            }
        } else {
            // No ramping
            current_left_speed = target_left_speed;
            current_right_speed = target_right_speed;
        }
        
        apply_motor_speed(current_left_speed, current_right_speed,
                          motor_cfg.invert_left, motor_cfg.invert_right);
        
        vTaskDelay(pdMS_TO_TICKS(loop_rate_ms));
    }
}

esp_err_t motor_bts7960_init(const motor_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "NULL config");
        return ESP_ERR_INVALID_ARG;
    }
    
    motor_cfg = *config;
    max_duty = pwm_ledc_get_max_duty(config->pwm_resolution);
    
    ESP_LOGI(TAG, "Initializing BTS7960 motor driver");
    ESP_LOGI(TAG, "  PWM: %lu Hz @ %d-bit (%lu max duty)", 
             config->pwm_freq_hz, config->pwm_resolution, max_duty);
    ESP_LOGI(TAG, "  Ramp rate: %lu ms", config->ramp_rate_ms);
    
    // Initialize PWM channels
    pwm_ledc_config_t pwm_cfg = {
        .freq_hz = config->pwm_freq_hz,
        .resolution = config->pwm_resolution,
    };
    
    pwm_cfg.gpio_num = config->left_rpwm;
    pwm_cfg.ledc_channel = CH_LEFT_RPWM;
    ESP_ERROR_CHECK(pwm_ledc_init(&pwm_cfg));
    
    pwm_cfg.gpio_num = config->left_lpwm;
    pwm_cfg.ledc_channel = CH_LEFT_LPWM;
    ESP_ERROR_CHECK(pwm_ledc_init(&pwm_cfg));
    
    pwm_cfg.gpio_num = config->right_rpwm;
    pwm_cfg.ledc_channel = CH_RIGHT_RPWM;
    ESP_ERROR_CHECK(pwm_ledc_init(&pwm_cfg));
    
    pwm_cfg.gpio_num = config->right_lpwm;
    pwm_cfg.ledc_channel = CH_RIGHT_LPWM;
    ESP_ERROR_CHECK(pwm_ledc_init(&pwm_cfg));
    
    // Initialize enable pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->left_ren) | (1ULL << config->left_len) |
                        (1ULL << config->right_ren) | (1ULL << config->right_len),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    
    // Enable all outputs
    gpio_set_level(config->left_ren, 1);
    gpio_set_level(config->left_len, 1);
    gpio_set_level(config->right_ren, 1);
    gpio_set_level(config->right_len, 1);
    
    // Start ramping task
    BaseType_t ret = xTaskCreate(motor_ramp_task, "motor_ramp",
                                  2048, NULL, 4, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ramp task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Motor driver initialized");
    return ESP_OK;
}

esp_err_t motor_set_speeds(float left_speed, float right_speed) {
    // Clamp speeds
    if (left_speed > 1.0f) left_speed = 1.0f;
    if (left_speed < -1.0f) left_speed = -1.0f;
    if (right_speed > 1.0f) right_speed = 1.0f;
    if (right_speed < -1.0f) right_speed = -1.0f;
    
    target_left_speed = left_speed;
    target_right_speed = right_speed;
    
    return ESP_OK;
}

esp_err_t motor_emergency_stop(void) {
    // Immediate stop (bypass ramping)
    target_left_speed = 0.0f;
    target_right_speed = 0.0f;
    current_left_speed = 0.0f;
    current_right_speed = 0.0f;
    
    apply_motor_speed(0.0f, 0.0f, false, false);
    
    ESP_LOGW(TAG, "EMERGENCY STOP");
    return ESP_OK;
}
