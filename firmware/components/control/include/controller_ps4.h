/**
 * @file controller_ps4.h
 * @brief PS4 controller interface
 *
 * Stub - wire up a PS4 Bluetooth library in components/ps4/ to enable.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Initialize PS4 controller
 *
 * @param mac_address 6-byte Bluetooth MAC address to advertise to controller
 * @return esp_err_t ESP_OK on success
 */
esp_err_t controller_ps4_init(const uint8_t *mac_address);
