/**
 * @file controller_serial.h
 * @brief Serial (UART) controller interface
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Initialize Serial controller
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t controller_serial_init(void);
