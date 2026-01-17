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
#include "controller_ps3.h"
#include "controller_serial.h"
#include "controller_http.h"
#include "motor_bts7960.h"
#include "mixer_diffdrive.h"
#include "safety_failsafe.h"

static const char *TAG = "main";

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
    ESP_LOGI(TAG, "  ESP32-WROVER-IE | BTS7960 | PS3 Controller");
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
        .invert_left = CONFIG_ROBOT_MOTOR_INVERT_LEFT,
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

#ifdef CONFIG_ROBOT_ENABLE_PS3
    // Initialize PS3 controller
    ESP_LOGI(TAG, "Initializing PS3 controller...");
    // NOTE: User must set their controller's MAC address here
    uint8_t ps3_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    ESP_LOGW(TAG, "*** REPLACE PS3 MAC ADDRESS IN main.c ***");
    ESP_LOGW(TAG, "Current MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             ps3_mac[0], ps3_mac[1], ps3_mac[2], 
             ps3_mac[3], ps3_mac[4], ps3_mac[5]);
    ESP_ERROR_CHECK(controller_ps3_init(ps3_mac));
#else
    ESP_LOGI(TAG, "PS3 controller disabled in config");
#endif

#ifdef CONFIG_ROBOT_ENABLE_SERIAL
    // Initialize Serial controller
    ESP_LOGI(TAG, "Initializing Serial controller...");
    ESP_ERROR_CHECK(controller_serial_init());
#else
    ESP_LOGI(TAG, "Serial controller disabled in config");
#endif

#ifdef CONFIG_ROBOT_ENABLE_HTTP
    // Initialize HTTP controller
    ESP_LOGI(TAG, "Initializing HTTP controller...");
    ESP_ERROR_CHECK(controller_http_init());
#else
    ESP_LOGI(TAG, "HTTP controller disabled in config");
#endif

    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "  System Ready");
    ESP_LOGI(TAG, "  State: DISARMED (press START to arm)");
    ESP_LOGI(TAG, "=================================================");

    // Main loop - monitor system health
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 second heartbeat
        ESP_LOGD(TAG, "Heartbeat: System running");
    }
}
