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
#include "dlm_types.h"
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
 * @brief Perform OTA update using pending update info
 *
 * Downloads, verifies, and installs the update.
 * After successful installation, device should be rebooted.
 *
 * @param progress_cb   Optional callback for progress updates (0-100)
 * @return ESP_OK on success
 */
esp_err_t ota_manager_perform_update(void (*progress_cb)(int percent));

/**
 * @brief Perform OTA update with explicit release info
 *
 * Same as ota_manager_perform_update but uses provided info instead
 * of the pending update from check_update.
 *
 * @param info          Release info (version, url, signature, etc.)
 * @param progress_cb   Optional callback for progress updates (0-100)
 * @return ESP_OK on success
 */
esp_err_t ota_manager_perform_update_with_info(const dlm_release_info_t *info,
                                                void (*progress_cb)(int percent));

/**
 * @brief Start OTA update in a background task
 *
 * Spawns a FreeRTOS task to download and install the update.
 * The task updates internal state/progress which can be polled
 * via ota_manager_get_state() and ota_manager_get_progress().
 * On success, the task reboots the device automatically.
 *
 * @param info  Release info
 * @return ESP_OK if task started, error otherwise
 */
esp_err_t ota_manager_start_update(const dlm_release_info_t *info);

/**
 * @brief Get current OTA state
 *
 * @return Current OTA state (idle, downloading, verifying, etc.)
 */
dlm_ota_state_t ota_manager_get_state(void);

/**
 * @brief Get current OTA progress (0-100)
 *
 * @return Progress percentage
 */
int ota_manager_get_progress(void);

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
