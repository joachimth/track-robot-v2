/**
 * @file ps4.h
 * @brief PS4 DualShock 4 controller driver (ESP-IDF esp_hidh)
 *
 * Scans for a PS4 "Wireless Controller" over Bluetooth Classic HID,
 * connects automatically, and invokes a callback with normalized input.
 *
 * To pair: press PS + Share on the controller while in scan mode.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Normalized PS4 gamepad state
 */
typedef struct {
    float lx;        ///< Left stick X:  -1.0 (left)  to +1.0 (right)
    float ly;        ///< Left stick Y:  -1.0 (up)    to +1.0 (down)
    float rx;        ///< Right stick X: -1.0 (left)  to +1.0 (right)
    float ry;        ///< Right stick Y: -1.0 (up)    to +1.0 (down)
    bool cross;      ///< X / Cross button
    bool circle;     ///< Circle button
    bool square;     ///< Square button
    bool triangle;   ///< Triangle button
    bool l1;         ///< L1 shoulder
    bool r1;         ///< R1 shoulder
    bool l2;         ///< L2 trigger (digital)
    bool r2;         ///< R2 trigger (digital)
    bool options;    ///< Options (Start) button
    bool ps;         ///< PS / Home button
    bool connected;  ///< true while a controller is connected
} ps4_gamepad_t;

/**
 * @brief Callback invoked on every HID input report
 *
 * Called from the hidh event task — keep it short and non-blocking.
 */
typedef void (*ps4_callback_t)(const ps4_gamepad_t *gamepad);

/**
 * @brief Initialize the PS4 driver and start BT scanning
 *
 * Initialises the Bluetooth controller + Bluedroid stack, then scans
 * for a PS4 "Wireless Controller" and connects automatically.
 *
 * @param host_mac  6-byte ESP32 Bluetooth MAC (NULL = use factory default)
 * @param callback  Function called on every input report (may be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ps4_init(const uint8_t *host_mac, ps4_callback_t callback);

/**
 * @brief Check whether a PS4 controller is currently connected
 */
bool ps4_is_connected(void);
