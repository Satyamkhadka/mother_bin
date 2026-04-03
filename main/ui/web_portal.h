/**
 * @file web_portal.h
 * @brief Web portal HTTP handlers and captive portal UI
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize web portal
 * 
 * Registers all HTTP routes for the captive portal.
 * 
 * @return ESP_OK on success
 */
esp_err_t web_portal_init(void);

/**
 * @brief Deinitialize web portal
 */
void web_portal_deinit(void);

#ifdef __cplusplus
}
#endif
