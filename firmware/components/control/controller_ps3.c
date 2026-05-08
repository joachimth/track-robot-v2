/**
 * @file controller_ps3.c
 * @brief PS3 controller implementation
 */

#include "controller_ps3.h"
#include "control_manager.h"
#include "control_frame.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// PS3 library
#include "ps3.h"

static const char *TAG = "ctrl_ps3";

static bool slow_mode_enabled = false;
static bool last_triangle_state = false;

/**
 * @brief Map PS3 analog stick (int8_t, -128..127) to normalized float (-1.0 to +1.0)
 */
static float map_stick(int8_t value) {
    return control_clamp(value / 127.0f);
}

/**
 * @brief PS3 event callback - called on every controller state change
 */
static void ps3_event_callback(ps3_t ps3, ps3_event_t event) {
    // Toggle slow mode on Triangle button press (edge trigger)
    bool triangle_pressed = ps3.button.triangle;
    if (triangle_pressed && !last_triangle_state) {
        slow_mode_enabled = !slow_mode_enabled;
        ESP_LOGI(TAG, "Slow mode: %s", slow_mode_enabled ? "ON" : "OFF");
    }
    last_triangle_state = triangle_pressed;

    // Build control frame using left stick (Y=throttle, X=steering)
    control_frame_t frame = {
        .throttle  = -map_stick(ps3.analog.stick.ly), // Invert Y: stick up = forward
        .steering  = map_stick(ps3.analog.stick.lx),  // Left stick X for steering
        .estop     = ps3.button.cross,                 // X button = e-stop
        .arm       = ps3.button.start,                 // START button = arm
        .slow_mode = slow_mode_enabled,
        .timestamp = xTaskGetTickCount(),
    };

    control_manager_submit(CONTROL_SOURCE_PS3, &frame);

    ESP_LOGD(TAG, "PS3: T=%.2f S=%.2f X=%d START=%d TRI=%d",
             frame.throttle, frame.steering, ps3.button.cross,
             ps3.button.start, ps3.button.triangle);
}

/**
 * @brief PS3 connection callback - called when controller connects or disconnects
 */
static void ps3_connection_callback(uint8_t is_connected) {
    if (is_connected) {
        ESP_LOGI(TAG, "PS3 controller connected!");
    } else {
        ESP_LOGW(TAG, "PS3 controller disconnected!");
        // Submit a zero frame so the failsafe timeout triggers
        control_frame_t frame = {0};
        frame.timestamp = xTaskGetTickCount();
        control_manager_submit(CONTROL_SOURCE_PS3, &frame);
    }
}

esp_err_t controller_ps3_init(const uint8_t *mac_address) {
    if (mac_address == NULL) {
        ESP_LOGE(TAG, "NULL MAC address");
        return ESP_ERR_INVALID_ARG;
    }

    // Set ESP32 Bluetooth address that the PS3 controller should pair to
    ps3SetBluetoothMacAddress(mac_address);

    // Register callbacks
    ps3SetEventCallback(ps3_event_callback);
    ps3SetConnectionCallback(ps3_connection_callback);

    // Start PS3 Bluetooth server
    ps3Init();

    ESP_LOGI(TAG, "PS3 controller initialized");
    ESP_LOGI(TAG, "  Waiting for connection... (hold PS button on controller)");

    return ESP_OK;
}
