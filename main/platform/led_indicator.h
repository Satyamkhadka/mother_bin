/**
 * @file led_indicator.h
 * @brief LED indicator API with pattern support
 */

#pragma once

#include "dlm_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LED indicator
 * 
 * Configures GPIO and starts pattern task.
 * 
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED if LED disabled
 */
esp_err_t led_indicator_init(void);

/**
 * @brief Deinitialize LED indicator
 * 
 * Stops pattern task and turns off LED.
 */
void led_indicator_deinit(void);

/**
 * @brief Set LED blinking pattern
 * 
 * @param pattern Pattern to display
 */
void led_set_pattern(led_pattern_t pattern);

/**
 * @brief Get current LED pattern
 * 
 * @return Current pattern
 */
led_pattern_t led_get_pattern(void);

/**
 * @brief Turn LED on (static, overrides pattern temporarily)
 */
void led_on_static(void);

/**
 * @brief Turn LED off (static, overrides pattern temporarily)
 */
void led_off_static(void);

/* ============== Convenience Functions ==============
 * Predefined patterns for common system states
 */

/**
 * @brief Indicate provisioning mode (waiting for WiFi config)
 * Pattern: Slow blink (1Hz)
 */
void led_indicate_provisioning(void);

/**
 * @brief Indicate connecting to WiFi
 * Pattern: Fast blink (4Hz)
 */
void led_indicate_connecting(void);

/**
 * @brief Indicate connected and operational
 * Pattern: Double blink
 */
void led_indicate_connected(void);

/**
 * @brief Indicate OTA update in progress
 * Pattern: Triple blink
 */
void led_indicate_updating(void);

/**
 * @brief Indicate error state
 * Pattern: Rapid flashing
 */
void led_indicate_error(void);

/**
 * @brief Indicate success (brief solid on)
 * Pattern: Solid for 2 seconds (caller should manage transition back)
 */
void led_indicate_success(void);

#ifdef __cplusplus
}
#endif
