/**
 * @file pwm_ledc.c
 * @brief ESP32 LEDC PWM driver implementation
 */

#include "pwm_ledc.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "pwm_ledc";

esp_err_t pwm_ledc_init(const pwm_ledc_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "NULL config");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Configure timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = config->resolution,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = config->freq_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure channel
    ledc_channel_config_t ch_conf = {
        .gpio_num = config->gpio_num,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = config->ledc_channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ch_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "PWM initialized: GPIO=%d CH=%d FREQ=%lu RES=%d",
             config->gpio_num, config->ledc_channel, config->freq_hz, config->resolution);
    
    return ESP_OK;
}

esp_err_t pwm_ledc_set_duty(uint8_t channel, uint32_t duty) {
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}
