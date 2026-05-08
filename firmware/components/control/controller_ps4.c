/**
 * @file controller_ps4.c
 * @brief PS4 DualShock 4 controller input handler
 *
 * Maps gamepad state to control_frame_t:
 *   Left stick Y (inverted) -> throttle
 *   Left stick X            -> steering
 *   Options                 -> arm
 *   Cross                   -> emergency stop
 *   L1                      -> slow mode
 */

#include "controller_ps4.h"
#include "control_manager.h"
#include "control_frame.h"
#include "ps4.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ctrl_ps4";

static void ps4_input_cb(const ps4_gamepad_t *g) {
    control_frame_t frame = {0};
    frame.timestamp = xTaskGetTickCount();

    // Left stick Y is inverted: push up (negative ly) = forward
    frame.throttle  = control_clamp(-g->ly);
    frame.steering  = control_clamp(g->lx);
    frame.slow_mode = g->l1;
    frame.arm       = g->options;
    frame.estop     = g->cross;

    control_manager_submit(CONTROL_SOURCE_PS4, &frame);
}

esp_err_t controller_ps4_init(const uint8_t *mac_address) {
    ESP_LOGI(TAG, "Initializing PS4 controller");
    return ps4_init(mac_address, ps4_input_cb);
}
