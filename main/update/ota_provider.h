/**
 * @file ota_provider.h
 * @brief Abstract OTA Provider Interface
 * 
 * This defines the interface for firmware update sources.
 * Implement this interface to add new update sources.
 * 
 * Included providers:
 * - github_provider: Fetch updates from GitHub releases
 * - custom_provider: Fetch from your own server (PLACEHOLDER)
 */

#pragma once

#include "dlm_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA Provider interface
 * 
 * All providers must implement these functions.
 */
typedef struct {
    const char *name;   /**< Provider name for logging */
    
    /**
     * @brief Query for available firmware update
     * 
     * @param server_config     Provider-specific config (repo name, URL, etc.)
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
     * The provider should use esp_https_ota or similar to download
     * and write the firmware to the OTA partition.
     * 
     * @param download_url      URL to firmware binary
     * @param progress_cb       Optional progress callback (0-100 percent)
     * @return ESP_OK on success
     */
    esp_err_t (*download)(const char *download_url,
                          void (*progress_cb)(int percent));
    
} ota_provider_t;

/**
 * @brief Get the GitHub releases provider
 * 
 * @return Pointer to GitHub provider
 */
const ota_provider_t* ota_provider_get_github(void);

/**
 * @brief Get the custom server provider (your implementation)
 * 
 * This is a PLACEHOLDER for your custom server implementation.
 * Implement your own API in custom_provider.c
 * 
 * @return Pointer to custom provider
 */
const ota_provider_t* ota_provider_get_custom(void);

/**
 * @brief Get provider by name
 * 
 * @param name  Provider name ("github" or "custom")
 * @return Provider pointer, or NULL if not found
 */
const ota_provider_t* ota_provider_get_by_name(const char *name);

#ifdef __cplusplus
}
#endif
