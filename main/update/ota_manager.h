/**
 * @file ota_manager.h
 * @brief OTA update orchestration
 *
 * Coordinates the update process:
 * 1. Query provider for available update
 * 2. Download firmware
 * 3. Verify signature
 * 4. Install and reboot
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize OTA manager
 *
 * Loads configuration and initializes signature verifier.
 *
 * @return ESP_OK on success
 */
esp_err_t ota_manager_init(void);

/**
 * @brief Check for available firmware update
 *
 * Queries the firmware-manager backend. The server decides whether an
 * update is available based on device_id, hardware_version and chip_model.
 *
 * @param update_available  Output: true if update is available
 * @return ESP_OK on success (even if no update), error code on query failure
 */
esp_err_t ota_manager_check_update(bool *update_available);

/**
 * @brief Perform OTA update
 *
 * Downloads, verifies, and installs the update.
 * After successful installation, device should be rebooted.
 *
 * @param progress_cb   Optional callback for progress updates (0-100)
 * @return ESP_OK on success
 */
esp_err_t ota_manager_perform_update(void (*progress_cb)(int percent));

/**
 * @brief Get current firmware version
 *
 * @param version   Output buffer
 * @param len       Buffer size
 * @return ESP_OK on success
 */
esp_err_t ota_manager_get_current_version(char *version, size_t len);

#ifdef __cplusplus
}
#endif
