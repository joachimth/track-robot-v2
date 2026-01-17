/**
 * @file safety_failsafe.h
 * @brief Safety and failsafe system
 * 
 * Manages arming state, emergency stop, and failsafe timeout.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Safety state
 */
typedef enum {
    SAFETY_STATE_DISARMED = 0,  ///< Motors disabled (default at boot)
    SAFETY_STATE_ARMED = 1,     ///< Motors enabled
    SAFETY_STATE_ESTOP = 2,     ///< Emergency stop (latched)
} safety_state_t;

/**
 * @brief Initialize safety system
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t safety_failsafe_init(void);

/**
 * @brief Check if motors are allowed to move
 * 
 * @return bool True if armed and not e-stopped
 */
bool safety_is_armed(void);

/**
 * @brief Arm the system (enable motors)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t safety_arm(void);

/**
 * @brief Disarm the system (disable motors)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t safety_disarm(void);

/**
 * @brief Trigger emergency stop (latched)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t safety_emergency_stop(void);

/**
 * @brief Update failsafe watchdog (call on valid control input)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t safety_update_watchdog(void);
