/**
 * @file control_manager.h
 * @brief Control arbitration manager
 * 
 * Implements "owner lock" model: last active source takes control until timeout.
 */

#pragma once

#include "esp_err.h"
#include "control_frame.h"

/**
 * @brief Initialize control manager
 */
esp_err_t control_manager_init(void);

/**
 * @brief Submit control frame from a source
 * 
 * @param source Control source ID
 * @param frame Control frame data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t control_manager_submit(control_source_t source, const control_frame_t *frame);

/**
 * @brief Get current active control source
 *
 * @return control_source_t Active source (CONTROL_SOURCE_NONE if timeout)
 */
control_source_t control_manager_get_active_source(void);

/**
 * @brief Snapshot of the full system state (input + output)
 */
typedef struct {
    control_source_t source;       ///< Active control source
    control_frame_t  frame;        ///< Last submitted control frame
    float            left_output;  ///< Mixed left motor speed sent to motor driver
    float            right_output; ///< Mixed right motor speed sent to motor driver
} control_status_t;

/**
 * @brief Get a consistent snapshot of the current control status
 *
 * @param out Caller-allocated struct to fill
 */
void control_manager_get_status(control_status_t *out);
