/**
 * @file controller_ps3.h
 * @brief PS3 controller interface
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Initialize PS3 controller
 * 
 * @param mac_address PS3 controller Bluetooth MAC address (6 bytes)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t controller_ps3_init(const uint8_t *mac_address);
