/**
 * @file controller_ps4.c
 * @brief PS4 controller stub
 *
 * This is a placeholder. To add PS4 support:
 * 1. Add a PS4 Bluetooth library to firmware/components/ps4/
 *    (e.g. https://github.com/aed3/psx-esp32 or similar)
 * 2. Create firmware/components/ps4/CMakeLists.txt
 * 3. Replace this stub with the actual implementation
 * 4. Set CONFIG_ROBOT_ENABLE_PS4=y in menuconfig
 */

#include "controller_ps4.h"
#include "control_manager.h"
#include "control_frame.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ctrl_ps4";

esp_err_t controller_ps4_init(const uint8_t *mac_address) {
    if (mac_address == NULL) {
        ESP_LOGE(TAG, "NULL MAC address");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG, "PS4 controller support is a stub - add PS4 library to components/ps4/");
    ESP_LOGW(TAG, "See firmware/components/control/controller_ps4.c for instructions");

    return ESP_OK;
}
