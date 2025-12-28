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

// PS3 library (external component)
#include "ps3.h"

static const char *TAG = "ctrl_ps3";

// State
static bool slow_mode_enabled = false;
static bool last_triangle_state = false;

/**
 * @brief Map PS3 analog stick to normalized float (-1.0 to +1.0)
 */
static float map_analog(int value) {
    // PS3 analog: 0-255, center at 128
    float normalized = (value - 128) / 128.0f;
    return control_clamp(normalized);
}

/**
 * @brief PS3 event callback
 */
static void ps3_event_callback(ps3_t ps3, ps3_event_t event) {
    if (event != PS3_EVENT_NOTIFICATION) {
        return; // Only process notification events (stick/button changes)
    }
    
    // Toggle slow mode on Triangle button press (edge trigger)
    bool triangle_pressed = ps3.button.triangle;
    if (triangle_pressed && !last_triangle_state) {
        slow_mode_enabled = !slow_mode_enabled;
        ESP_LOGI(TAG, "Slow mode: %s", slow_mode_enabled ? "ON" : "OFF");
    }
    last_triangle_state = triangle_pressed;
    
    // Build control frame
    control_frame_t frame = {
        .throttle = -map_analog(ps3.analog.stick.ly),  // Invert Y (up = positive)
        .steering = map_analog(ps3.analog.stick.rx),   // Right stick X for steering
        .estop = ps3.button.cross,                     // X button = e-stop
        .arm = ps3.button.start,                       // Start button = arm
        .slow_mode = slow_mode_enabled,
        .timestamp = xTaskGetTickCount(),
    };
    
    // Submit to control manager
    control_manager_submit(CONTROL_SOURCE_PS3, &frame);
    
    // Log debug info
    ESP_LOGD(TAG, "PS3: T=%.2f S=%.2f X=%d START=%d TRI=%d",
             frame.throttle, frame.steering, ps3.button.cross,
             ps3.button.start, ps3.button.triangle);
}

/**
 * @brief PS3 connection callback
 */
static void ps3_connection_callback(ps3_t ps3, ps3_event_t event) {
    if (event == PS3_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "PS3 controller connected!");
        
        // Get controller info
        ESP_LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                 ps3.mac_address[0], ps3.mac_address[1], ps3.mac_address[2],
                 ps3.mac_address[3], ps3.mac_address[4], ps3.mac_address[5]);
        ESP_LOGI(TAG, "  Battery: %d%%", ps3.status.battery);
        
    } else if (event == PS3_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "PS3 controller disconnected!");
        
        // Submit zero frame to trigger timeout
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
    
    // Set MAC address
    ps3SetBluetoothMacAddress(mac_address);
    
    // Register callbacks
    ps3SetEventCallback(ps3_event_callback);
    ps3SetConnectionCallback(ps3_connection_callback);
    
    // Initialize PS3 library
    ps3Init();
    
    ESP_LOGI(TAG, "PS3 controller initialized");
    ESP_LOGI(TAG, "  Waiting for connection... (press PS button)");
    
    return ESP_OK;
}
