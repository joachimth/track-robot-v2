/**
 * @file control_frame.h
 * @brief Control frame interface - common structure for all control sources
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Control source identifiers
 */
typedef enum {
    CONTROL_SOURCE_NONE = 0,
    CONTROL_SOURCE_PS3 = 1,
    CONTROL_SOURCE_SERIAL = 2,
    CONTROL_SOURCE_HTTP = 3,
} control_source_t;

/**
 * @brief Normalized control frame
 * 
 * All control sources must produce this standardized frame.
 * Values are normalized to [-1.0, +1.0] range.
 */
typedef struct {
    float throttle;      ///< Forward/backward: -1.0 (reverse) to +1.0 (forward)
    float steering;      ///< Left/right: -1.0 (left) to +1.0 (right)
    bool estop;          ///< Emergency stop command
    bool arm;            ///< Arming command
    bool slow_mode;      ///< Slow mode toggle
    uint32_t timestamp;  ///< Frame timestamp (xTaskGetTickCount())
} control_frame_t;

/**
 * @brief Clamp float value to [-1.0, +1.0]
 */
static inline float control_clamp(float value) {
    if (value > 1.0f) return 1.0f;
    if (value < -1.0f) return -1.0f;
    return value;
}
