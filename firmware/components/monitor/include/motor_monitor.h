/**
 * @file motor_monitor.h
 * @brief Motor current-sense + battery voltage monitoring
 *
 * Samples the IBT-2 R_IS/L_IS current-sense pins (ADC) for both motors at
 * ~10 Hz and an optional battery voltage divider at ~1 Hz. Logs warnings on
 * over-current / low-battery and latches an emergency stop on over-current.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Latest monitoring readings
 */
typedef struct {
    uint32_t left_ma;          ///< Left motor current (max of its two IS pins), mA
    uint32_t right_ma;         ///< Right motor current (max of its two IS pins), mA
    bool     overcurrent;      ///< True if last sample exceeded the threshold
    uint32_t battery_mv;       ///< Battery voltage in mV (0 if disabled)
    bool     battery_enabled;  ///< True if battery monitoring is configured
    bool     battery_low;      ///< True if battery is below the low threshold
} monitor_status_t;

/**
 * @brief Initialize the monitoring subsystem and start the sampling task
 *
 * Safe to call once. If no ADC channels can be configured, returns an error
 * and the subsystem stays inactive (status reads return zeros).
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t motor_monitor_init(void);

/**
 * @brief Copy the latest monitoring readings
 *
 * Safe to call before init — fills the struct with zeros in that case.
 *
 * @param out Destination struct (must not be NULL)
 */
void motor_monitor_get_status(monitor_status_t *out);
