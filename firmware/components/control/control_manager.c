/**
 * @file control_manager.c
 * @brief Control arbitration manager implementation
 */

#include "control_manager.h"
#include "safety_failsafe.h"
#include "mixer_diffdrive.h"
#include "motor_bts7960.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "control_mgr";

// Current state
static control_source_t active_source = CONTROL_SOURCE_NONE;
static control_frame_t current_frame = {0};
static SemaphoreHandle_t mutex = NULL;
static uint32_t last_update_tick = 0;

// Constants
#define CONTROL_TASK_STACK_SIZE 4096
#define CONTROL_TASK_PRIORITY 5
#define CONTROL_LOOP_RATE_MS 20  // 50Hz control loop

/**
 * @brief Control loop task
 */
static void control_task(void *arg) {
    float left_speed, right_speed;
    
    while (1) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        // Check timeout
        uint32_t now = xTaskGetTickCount();
        uint32_t timeout_ticks = pdMS_TO_TICKS(CONFIG_ROBOT_FAILSAFE_TIMEOUT_MS);
        
        if (active_source != CONTROL_SOURCE_NONE && 
            (now - last_update_tick) > timeout_ticks) {
            ESP_LOGW(TAG, "Control timeout! Source %d inactive for %lu ms",
                     active_source, (now - last_update_tick) * portTICK_PERIOD_MS);
            active_source = CONTROL_SOURCE_NONE;
            memset(&current_frame, 0, sizeof(current_frame));
        }
        
        // Handle emergency stop
        if (current_frame.estop) {
            safety_emergency_stop();
            xSemaphoreGive(mutex);
            vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_RATE_MS));
            continue;
        }
        
        // Handle arming
        if (current_frame.arm) {
            safety_arm();
        }
        
        // Update watchdog
        if (active_source != CONTROL_SOURCE_NONE) {
            safety_update_watchdog();
        }
        
        // Mix and send to motors (only if armed)
        if (safety_is_armed()) {
            mixer_diffdrive_mix(current_frame.throttle, current_frame.steering,
                                current_frame.slow_mode, &left_speed, &right_speed);
            motor_set_speeds(left_speed, right_speed);
            
            ESP_LOGD(TAG, "Control: src=%d t=%.2f s=%.2f -> L=%.2f R=%.2f slow=%d",
                     active_source, current_frame.throttle, current_frame.steering,
                     left_speed, right_speed, current_frame.slow_mode);
        } else {
            motor_set_speeds(0.0f, 0.0f);
        }
        
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_RATE_MS));
    }
}

esp_err_t control_manager_init(void) {
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }
    
    // Start control task
    BaseType_t ret = xTaskCreate(control_task, "control_task",
                                  CONTROL_TASK_STACK_SIZE, NULL,
                                  CONTROL_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create control task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Control manager initialized (loop rate: %d ms)", CONTROL_LOOP_RATE_MS);
    return ESP_OK;
}

esp_err_t control_manager_submit(control_source_t source, const control_frame_t *frame) {
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    // Update active source (last one wins)
    if (source != active_source) {
        ESP_LOGI(TAG, "Control source changed: %d -> %d", active_source, source);
        active_source = source;
    }
    
    // Update frame
    memcpy(&current_frame, frame, sizeof(control_frame_t));
    last_update_tick = xTaskGetTickCount();
    
    xSemaphoreGive(mutex);
    return ESP_OK;
}

control_source_t control_manager_get_active_source(void) {
    control_source_t source;
    xSemaphoreTake(mutex, portMAX_DELAY);
    source = active_source;
    xSemaphoreGive(mutex);
    return source;
}
