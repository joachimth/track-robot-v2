/**
 * @file ps4.h
 * @brief Generic Bluetooth gamepad driver backed by Bluepad32
 *
 * The public API keeps the historical ps4_* naming so the rest of the robot
 * firmware does not need to know which Bluetooth backend is used.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Normalized gamepad state
 */
typedef struct {
    float lx;        ///< Left stick X:  -1.0 (left)  to +1.0 (right)
    float ly;        ///< Left stick Y:  -1.0 (up)    to +1.0 (down)
    float rx;        ///< Right stick X: -1.0 (left)  to +1.0 (right)
    float ry;        ///< Right stick Y: -1.0 (up)    to +1.0 (down)

    bool cross;      ///< South / Cross / A button
    bool circle;     ///< East / Circle / B button
    bool square;     ///< West / Square / X button
    bool triangle;   ///< North / Triangle / Y button

    bool l1;         ///< L1 / left shoulder
    bool r1;         ///< R1 / right shoulder
    bool l2;         ///< L2 / left trigger digital
    bool r2;         ///< R2 / right trigger digital

    bool share;      ///< Share / Select / Back
    bool options;    ///< Options / Start
    bool ps;         ///< PS / Home / System

    bool dpad_up;
    bool dpad_down;
    bool dpad_left;
    bool dpad_right;

    bool connected;  ///< true while a controller is connected
} ps4_gamepad_t;

typedef void (*ps4_callback_t)(const ps4_gamepad_t *gamepad);

esp_err_t ps4_init(const uint8_t *host_mac, ps4_callback_t callback);
bool ps4_is_connected(void);
