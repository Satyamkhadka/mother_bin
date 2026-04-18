/**
 * @file ota_provider.h
 * @brief OTA Provider Interface
 *
 * The firmware-manager backend is the sole update source.
 * The interface is kept for testability.
 */

#pragma once

#include "dlm_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA Provider interface
 */
typedef struct {
    const char *name;

    /**
     * @brief Query for available firmware update
     *
     * @param server_config     Backend base URL
     * @param current_version   Current firmware version (semver)
     * @param out_info          Output: release information
     * @return ESP_OK if update available, ESP_ERR_NOT_FOUND if no update,
     *         other error codes on failure
     */
    esp_err_t (*query)(const char *server_config,
                       const char *current_version,
                       dlm_release_info_t *out_info);

    /**
     * @brief Download and install firmware
     *
     * @param download_url      URL to firmware binary
     * @param progress_cb       Optional progress callback (0-100 percent)
     * @return ESP_OK on success
     */
    esp_err_t (*download)(const char *download_url,
                          void (*progress_cb)(int percent));

} ota_provider_t;

/**
 * @brief Get the firmware-manager provider instance
 */
const ota_provider_t* ota_provider_get_custom(void);

/**
 * @brief Get provider by name (always returns the firmware-manager provider)
 */
const ota_provider_t* ota_provider_get_by_name(const char *name);

#ifdef __cplusplus
}
#endif
