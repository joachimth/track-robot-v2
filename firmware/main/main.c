/**
 * @file main.c
 * @brief Tracked Robot Firmware - Main Application
 *
 * Initializes all subsystems and starts control tasks.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

// Component includes
#include "control_manager.h"
#include "controller_serial.h"
#include "controller_http.h"
#include "motor_bts7960.h"
#include "mixer_diffdrive.h"
#include "safety_failsafe.h"
#ifdef CONFIG_ROBOT_ENABLE_PS4
#include "controller_ps4.h"
#endif

static const char *TAG = "main";

// ESP-IDF does not define disabled bool Kconfig symbols - provide 0 fallbacks
#ifndef CONFIG_ROBOT_MOTOR_INVERT_LEFT
#define CONFIG_ROBOT_MOTOR_INVERT_LEFT 0
#endif
#ifndef CONFIG_ROBOT_MOTOR_INVERT_RIGHT
#define CONFIG_ROBOT_MOTOR_INVERT_RIGHT 0
#endif

#ifdef CONFIG_ROBOT_ENABLE_PS4
static void ps4_init_task(void *arg) {
    ESP_LOGI(TAG, "PS4 init task started");

    esp_err_t ret = controller_ps4_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PS4 controller init failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "PS4 controller init task completed");
    }

    vTaskDelete(NULL);
}
#endif

/**
 * @brief Initialize NVS (Non-Volatile Storage)
 */
static void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
}

/**
 * @brief Main application entry point
 */
void app_main(void) {
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  Tracked Robot Firmware v0.1.0");
    ESP_LOGI(TAG, "  ESP32-WROOM-32 | IBT-2 (BTS7960) | PS4");
    ESP_LOGI(TAG, "=================================================");

    // Initialize NVS
    init_nvs();

    // Initialize safety system FIRST (motors disarmed by default)
    ESP_LOGI(TAG, "Initializing safety system...");
    ESP_ERROR_CHECK(safety_failsafe_init());

    // Initialize motor control
    ESP_LOGI(TAG, "Initializing motor control...");
    motor_config_t motor_cfg = {
        .left_rpwm = CONFIG_ROBOT_MOTOR_LEFT_RPWM,
        .left_lpwm = CONFIG_ROBOT_MOTOR_LEFT_LPWM,
        .left_ren = CONFIG_ROBOT_MOTOR_LEFT_REN,
        .left_len = CONFIG_ROBOT_MOTOR_LEFT_LEN,
        .right_rpwm = CONFIG_ROBOT_MOTOR_RIGHT_RPWM,
        .right_lpwm = CONFIG_ROBOT_MOTOR_RIGHT_LPWM,
        .right_ren = CONFIG_ROBOT_MOTOR_RIGHT_REN,
        .right_len = CONFIG_ROBOT_MOTOR_RIGHT_LEN,
        .pwm_freq_hz = CONFIG_ROBOT_MOTOR_PWM_FREQ_HZ,
        .pwm_resolution = CONFIG_ROBOT_MOTOR_PWM_RESOLUTION,
        .ramp_rate_ms = CONFIG_ROBOT_MOTOR_RAMP_RATE_MS,
        .invert_left  = CONFIG_ROBOT_MOTOR_INVERT_LEFT,
        .invert_right = CONFIG_ROBOT_MOTOR_INVERT_RIGHT,
    };
    ESP_ERROR_CHECK(motor_bts7960_init(&motor_cfg));

    // Initialize differential drive mixer
    ESP_LOGI(TAG, "Initializing differential drive...");
    mixer_config_t mixer_cfg = {
        .deadzone = CONFIG_ROBOT_DRIVE_DEADZONE / 100.0f,
        .expo = CONFIG_ROBOT_DRIVE_EXPO / 100.0f,
        .max_speed = CONFIG_ROBOT_DRIVE_MAX_SPEED / 100.0f,
        .slow_mode_factor = CONFIG_ROBOT_DRIVE_SLOW_MODE_FACTOR / 100.0f,
    };
    ESP_ERROR_CHECK(mixer_diffdrive_init(&mixer_cfg));

    // Initialize control manager (arbitration logic)
    ESP_LOGI(TAG, "Initializing control manager...");
    ESP_ERROR_CHECK(control_manager_init());

#ifdef CONFIG_ROBOT_ENABLE_SERIAL
    // Initialize Serial controller
    ESP_LOGI(TAG, "Initializing Serial controller...");
    ESP_ERROR_CHECK(controller_serial_init());
#else
    ESP_LOGI(TAG, "Serial controller disabled in config");
#endif

#ifdef CONFIG_ROBOT_ENABLE_HTTP
    // Initialize HTTP controller before PS4 so AP/web UI is always available.
    ESP_LOGI(TAG, "Initializing HTTP/WiFi controller...");
    ESP_ERROR_CHECK(controller_http_init());
#else
    ESP_LOGI(TAG, "HTTP controller disabled in config");
#endif

#ifdef CONFIG_ROBOT_ENABLE_PS4
    // Run PS4 HID host initialization in a separate task so a BT/HID issue
    // cannot block WiFi fallback, web UI, serial control, or system heartbeat.
    ESP_LOGI(TAG, "Starting PS4 controller init task...");
    BaseType_t ps4_task = xTaskCreate(ps4_init_task, "ps4_init", 6144, NULL, 4, NULL);
    if (ps4_task != pdPASS) {
        ESP_LOGE(TAG, "Failed to create PS4 init task");
    }
#else
    ESP_LOGI(TAG, "PS4 controller disabled in config");
#endif

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  System Ready");
    ESP_LOGI(TAG, "  State: DISARMED (press Options on PS4 to arm)");
    ESP_LOGI(TAG, "  Fallback AP: TrackRobot-Setup / trackrobot");
    ESP_LOGI(TAG, "  Web UI: http://192.168.4.1/");
    ESP_LOGI(TAG, "=================================================");

    // Main loop - monitor system health
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 second heartbeat
        ESP_LOGD(TAG, "Heartbeat: System running");
    }
}
