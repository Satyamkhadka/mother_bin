/**
 * @file boot_manager.h
 * @brief Boot sequence orchestration and application firmware boot
 */

#pragma once

#include "dlm_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Boot phases - track where we are in boot sequence
 */
typedef enum {
    BOOT_PHASE_INIT,            /* Early init, checking reset conditions */
    BOOT_PHASE_CHECK_RESET,     /* Factory reset detection */
    BOOT_PHASE_LOAD_CONFIG,     /* Load configuration from NVS */
    BOOT_PHASE_WIFI_INIT,       /* Initialize WiFi subsystem */
    BOOT_PHASE_PROVISIONING,    /* AP mode, waiting for user config */
    BOOT_PHASE_CONNECTING,      /* Connecting to configured WiFi */
    BOOT_PHASE_CONNECTED,       /* WiFi connected */
    BOOT_PHASE_OTA_CHECK,       /* Checking for firmware update */
    BOOT_PHASE_OTA_UPDATE,      /* Downloading/installing update */
    BOOT_PHASE_APP_BOOT,        /* Booting application firmware */
    BOOT_PHASE_ERROR,           /* Fatal error, cannot proceed */
} boot_phase_t;

/**
 * @brief Boot result - passed to callback when boot sequence completes
 */
typedef enum {
    BOOT_RESULT_SUCCESS,        /* Successfully booted application */
    BOOT_RESULT_NEED_PROVISION, /* Waiting for user provisioning */
    BOOT_RESULT_UPDATING,       /* OTA update in progress, will reboot */
    BOOT_RESULT_ERROR,          /* Fatal error occurred */
} boot_result_t;

/**
 * @brief Boot completion callback
 * 
 * Called when boot sequence completes or needs user interaction.
 * 
 * @param result    Boot result
 * @param message   Human-readable status message
 */
typedef void (*boot_complete_cb_t)(boot_result_t result, const char *message);

/**
 * @brief Initialize boot manager
 * 
 * Must be called after NVS is initialized.
 * 
 * @return ESP_OK on success
 */
esp_err_t boot_manager_init(void);

/**
 * @brief Start boot sequence
 * 
 * This is the main entry point for the boot process.
 * It will:
 * 1. Check for factory reset conditions
 * 2. Load configuration
 * 3. Attempt WiFi connection or start AP mode
 * 4. Check for OTA updates
 * 5. Boot application firmware
 * 
 * This function returns immediately and runs asynchronously.
 * The callback is invoked when the sequence completes or needs input.
 * 
 * @param callback  Function to call on completion or when input needed
 * @return ESP_OK if sequence started successfully
 */
esp_err_t boot_manager_start(boot_complete_cb_t callback);

/**
 * @brief Get current boot phase
 * 
 * @return Current phase
 */
boot_phase_t boot_manager_get_phase(void);

/**
 * @brief Get current boot phase as human-readable string
 * 
 * @return Phase description
 */
const char* boot_manager_get_phase_str(void);

/**
 * @brief Notify boot manager that WiFi provisioning is complete
 * 
 * Call this when user has submitted WiFi credentials via web UI.
 * 
 * @param ssid      WiFi SSID
 * @param password  WiFi password (can be empty string for open networks)
 * @return ESP_OK on success
 */
esp_err_t boot_manager_provisioning_complete(const char *ssid, const char *password);

/**
 * @notify boot manager to retry WiFi connection
 * 
 * Called after provisioning or on connection failure retry.
 */
void boot_manager_retry_connection(void);

/**
 * @brief Notify boot manager that node configuration is complete
 * 
 * Called when user has claimed or manually configured node credentials.
 * Triggers OTA check and application boot.
 */
void boot_manager_node_config_complete(void);

/**
 * @brief Boot application firmware from OTA partition
 * 
 * This function does not return if successful - it boots the app.
 * 
 * @return ESP_FAIL if boot failed (only returns on error)
 */
esp_err_t boot_manager_boot_application(void);

/**
 * @brief Check if application firmware is present
 * 
 * @return true if valid application found in OTA partition
 */
bool boot_manager_has_application(void);

/**
 * @brief Get current boot state for LED indication
 * 
 * Maps internal boot phase to LED pattern.
 * 
 * @return LED pattern for current state
 */
led_pattern_t boot_manager_get_led_pattern(void);

#ifdef __cplusplus
}
#endif
