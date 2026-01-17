/**
 * @file safety_failsafe.c
 * @brief Safety and failsafe system implementation
 */

#include "safety_failsafe.h"
#include "motor_bts7960.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "safety";

// State
static safety_state_t current_state = SAFETY_STATE_DISARMED;
static uint32_t last_watchdog_tick = 0;

// LED patterns
typedef enum {
    LED_PATTERN_BOOT,
    LED_PATTERN_DISARMED,
    LED_PATTERN_ARMED,
    LED_PATTERN_ESTOP,
} led_pattern_t;

static led_pattern_t current_led_pattern = LED_PATTERN_BOOT;

/**
 * @brief LED task
 */
static void led_task(void *arg) {
    if (CONFIG_ROBOT_STATUS_LED_PIN < 0) {
        vTaskDelete(NULL);
        return;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_ROBOT_STATUS_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    while (1) {
        switch (current_led_pattern) {
            case LED_PATTERN_BOOT:
                // Fast blink during boot
                gpio_set_level(CONFIG_ROBOT_STATUS_LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level(CONFIG_ROBOT_STATUS_LED_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case LED_PATTERN_DISARMED:
                // Slow blink when disarmed
                gpio_set_level(CONFIG_ROBOT_STATUS_LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(1000));
                gpio_set_level(CONFIG_ROBOT_STATUS_LED_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
                
            case LED_PATTERN_ARMED:
                // Solid ON when armed
                gpio_set_level(CONFIG_ROBOT_STATUS_LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
                
            case LED_PATTERN_ESTOP:
                // Very fast blink on e-stop
                gpio_set_level(CONFIG_ROBOT_STATUS_LED_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(50));
                gpio_set_level(CONFIG_ROBOT_STATUS_LED_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
        }
    }
}

/**
 * @brief Failsafe watchdog task
 */
static void watchdog_task(void *arg) {
    while (1) {
        uint32_t now = xTaskGetTickCount();
        uint32_t timeout_ticks = pdMS_TO_TICKS(CONFIG_ROBOT_FAILSAFE_TIMEOUT_MS);
        
        if (current_state == SAFETY_STATE_ARMED) {
            if ((now - last_watchdog_tick) > timeout_ticks) {
                ESP_LOGW(TAG, "Watchdog timeout! Auto-disarming...");
                safety_disarm();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
    }
}

esp_err_t safety_failsafe_init(void) {
    current_state = SAFETY_STATE_DISARMED;
    last_watchdog_tick = xTaskGetTickCount();
    current_led_pattern = LED_PATTERN_BOOT;
    
    // Start LED task
    xTaskCreate(led_task, "led_task", 2048, NULL, 3, NULL);
    
    // Start watchdog task
    xTaskCreate(watchdog_task, "watchdog_task", 2048, NULL, 5, NULL);
    
    // Set LED to disarmed after boot
    vTaskDelay(pdMS_TO_TICKS(2000)); // 2 second boot pattern
    current_led_pattern = LED_PATTERN_DISARMED;
    
    ESP_LOGI(TAG, "Safety system initialized");
    ESP_LOGI(TAG, "  Initial state: DISARMED");
    ESP_LOGI(TAG, "  Failsafe timeout: %d ms", CONFIG_ROBOT_FAILSAFE_TIMEOUT_MS);
    
    return ESP_OK;
}

bool safety_is_armed(void) {
    return current_state == SAFETY_STATE_ARMED;
}

esp_err_t safety_arm(void) {
    if (current_state == SAFETY_STATE_ESTOP) {
        ESP_LOGW(TAG, "Cannot arm: E-STOP active");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (current_state != SAFETY_STATE_ARMED) {
        current_state = SAFETY_STATE_ARMED;
        current_led_pattern = LED_PATTERN_ARMED;
        last_watchdog_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "System ARMED");
    }
    
    return ESP_OK;
}

esp_err_t safety_disarm(void) {
    if (current_state == SAFETY_STATE_ESTOP) {
        ESP_LOGW(TAG, "Cannot disarm: E-STOP active (use arm to clear)");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (current_state != SAFETY_STATE_DISARMED) {
        current_state = SAFETY_STATE_DISARMED;
        current_led_pattern = LED_PATTERN_DISARMED;
        motor_emergency_stop();
        ESP_LOGI(TAG, "System DISARMED");
    }
    
    return ESP_OK;
}

esp_err_t safety_emergency_stop(void) {
    current_state = SAFETY_STATE_ESTOP;
    current_led_pattern = LED_PATTERN_ESTOP;
    motor_emergency_stop();
    
    ESP_LOGE(TAG, "!!! EMERGENCY STOP !!!");
    ESP_LOGE(TAG, "Press ARM to clear and re-arm");
    
    return ESP_OK;
}

esp_err_t safety_update_watchdog(void) {
    last_watchdog_tick = xTaskGetTickCount();
    return ESP_OK;
}
