/**
 * @file controller_serial.c
 * @brief Serial (UART) controller implementation
 * 
 * Protocol: JSON lines, one command per line
 * Example: {"throttle": 0.5, "steering": -0.2}
 * Example: {"estop": true}
 * Example: {"arm": true}
 */

#include "controller_serial.h"
#include "control_manager.h"
#include "control_frame.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "ctrl_serial";

#define UART_NUM UART_NUM_0
#define UART_BUF_SIZE 256
#define SERIAL_TASK_STACK_SIZE 4096
#define SERIAL_TASK_PRIORITY 4

/**
 * @brief Parse JSON control command
 */
static esp_err_t parse_command(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGW(TAG, "Invalid JSON: %s", json_str);
        return ESP_FAIL;
    }
    
    control_frame_t frame = {0};
    frame.timestamp = xTaskGetTickCount();
    
    // Parse fields
    cJSON *throttle = cJSON_GetObjectItem(root, "throttle");
    if (throttle && cJSON_IsNumber(throttle)) {
        frame.throttle = control_clamp((float)throttle->valuedouble);
    }
    
    cJSON *steering = cJSON_GetObjectItem(root, "steering");
    if (steering && cJSON_IsNumber(steering)) {
        frame.steering = control_clamp((float)steering->valuedouble);
    }
    
    cJSON *estop = cJSON_GetObjectItem(root, "estop");
    if (estop && cJSON_IsBool(estop)) {
        frame.estop = cJSON_IsTrue(estop);
    }
    
    cJSON *arm = cJSON_GetObjectItem(root, "arm");
    if (arm && cJSON_IsBool(arm)) {
        frame.arm = cJSON_IsTrue(arm);
    }
    
    cJSON *slow_mode = cJSON_GetObjectItem(root, "slow_mode");
    if (slow_mode && cJSON_IsBool(slow_mode)) {
        frame.slow_mode = cJSON_IsTrue(slow_mode);
    }
    
    cJSON_Delete(root);
    
    // Submit to control manager
    control_manager_submit(CONTROL_SOURCE_SERIAL, &frame);
    
    ESP_LOGD(TAG, "Serial cmd: t=%.2f s=%.2f estop=%d arm=%d",
             frame.throttle, frame.steering, frame.estop, frame.arm);
    
    return ESP_OK;
}

/**
 * @brief Serial task
 */
static void serial_task(void *arg) {
    uint8_t data[UART_BUF_SIZE];
    char line_buf[UART_BUF_SIZE];
    int line_pos = 0;
    
    while (1) {
        int len = uart_read_bytes(UART_NUM, data, sizeof(data), pdMS_TO_TICKS(100));
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)data[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buf[line_pos] = '\0';
                        parse_command(line_buf);
                        line_pos = 0;
                    }
                } else if (line_pos < sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = c;
                }
            }
        }
    }
}

esp_err_t controller_serial_init(void) {
    // UART configuration
    uart_config_t uart_config = {
        .baud_rate = CONFIG_ROBOT_SERIAL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    
    // Start serial task
    BaseType_t ret = xTaskCreate(serial_task, "serial_task",
                                  SERIAL_TASK_STACK_SIZE, NULL,
                                  SERIAL_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create serial task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Serial controller initialized (baud: %d)", CONFIG_ROBOT_SERIAL_BAUD);
    ESP_LOGI(TAG, "  Protocol: JSON lines (e.g., {\"throttle\": 0.5, \"steering\": 0.0})");
    
    return ESP_OK;
}
