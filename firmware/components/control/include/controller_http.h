/**
 * @file controller_http.h
 * @brief HTTP (WiFi) controller interface
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Initialize HTTP controller
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t controller_http_init(void);
