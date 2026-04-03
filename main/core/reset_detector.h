/**
 * @file reset_detector.h
 * @brief Factory reset detection API
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if factory reset should be triggered
 * 
 * This function:
 * 1. Checks GPIO reset button (if configured)
 * 2. Checks reset counter (increments on every call)
 * 
 * Should be called once at boot. If it returns true,
 * call reset_detector_initiate_factory_reset().
 * 
 * @return true if factory reset should be performed
 */
bool reset_detector_triggered(void);

/**
 * @brief Clear the reset counter
 * 
 * Call this after successful WiFi connection or other
 * "device is configured" milestone to reset the counter.
 */
void reset_detector_clear(void);

/**
 * @brief Perform factory reset
 * 
 * Erases all configuration from NVS:
 * - WiFi credentials
 * - Device configuration
 * - OTA configuration
 * - Reset counter
 * 
 * After calling this, reboot the device.
 */
void reset_detector_initiate_factory_reset(void);

#ifdef __cplusplus
}
#endif
