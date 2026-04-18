/**
 * @file dlm_types.h
 * @brief Shared types and enums for Device Lifecycle Manager
 * 
 * This file contains type definitions used across all DLM modules.
 * Modifying this file may require recompiling the entire project.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Version ============== */
#define DLM_VERSION_MAJOR   1
#define DLM_VERSION_MINOR   0
#define DLM_VERSION_PATCH   0
#define DLM_VERSION_STRING  "1.0.0"

/* ============== Boot States ============== */

typedef enum {
    BOOT_STATE_FIRST_BOOT,          // First power-on, no config
    BOOT_STATE_PROVISIONING,        // In AP mode, waiting for config
    BOOT_STATE_CONNECTING,          // Connecting to WiFi
    BOOT_STATE_CHECKING_UPDATE,     // Connected, checking for OTA
    BOOT_STATE_UPDATING,            // Downloading/verifying firmware
    BOOT_STATE_BOOTING_APP,         // About to boot application
    BOOT_STATE_ERROR,               // Fatal error occurred
} dlm_boot_state_t;

/* ============== WiFi States ============== */

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_AP_MODE,             // Access Point mode (provisioning)
    WIFI_STATE_STA_CONNECTING,      // Station connecting
    WIFI_STATE_STA_CONNECTED,       // Station connected
    WIFI_STATE_STA_FAILED,          // Connection failed
    WIFI_STATE_AP_STA_CONCURRENT,   // AP + STA concurrent mode
} dlm_wifi_state_t;

/* ============== OTA States ============== */

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,             // Querying update server
    OTA_STATE_DOWNLOADING,          // Downloading firmware
    OTA_STATE_VERIFYING,            // Verifying signature
    OTA_STATE_COMPLETE,             // Ready to reboot
    OTA_STATE_FAILED,               // Update failed
} dlm_ota_state_t;

/* ============== LED Patterns ==============
 * Used to indicate system state via single LED blinking
 * Pattern format: on_time_ms, off_time_ms, repeat_count (0=infinite)
 */
typedef enum {
    LED_PATTERN_OFF,                // Solid off
    LED_PATTERN_ON,                 // Solid on
    LED_PATTERN_SLOW_BLINK,         // 1Hz (500ms on, 500ms off)
    LED_PATTERN_FAST_BLINK,         // 4Hz (125ms on, 125ms off)
    LED_PATTERN_DOUBLE_BLINK,       // Two quick blinks, pause
    LED_PATTERN_TRIPLE_BLINK,       // Three quick blinks, pause
    LED_PATTERN_BREATHING,          // PWM fade in/out (if hardware supports)
    LED_PATTERN_ERROR,              // Rapid flashing
} led_pattern_t;

/* ============== Configuration Field Types ============== */

typedef enum {
    CONFIG_FIELD_TYPE_STRING,
    CONFIG_FIELD_TYPE_NUMBER,
    CONFIG_FIELD_TYPE_BOOLEAN,
} config_field_type_t;

/* ============== Configuration Field Definition ============== */

typedef struct {
    const char *name;               // Key name (e.g., "device_name")
    const char *label;              // Display label (e.g., "Device Name")
    config_field_type_t type;       // Field type
    const char *default_str;        // Default value for string/bool
    int default_num;                // Default value for number
    int min_value;                  // Min for number validation
    int max_value;                  // Max for number validation
} config_field_def_t;

/* ============== Release Information ==============
 * Returned by OTA provider when querying for updates
 */
typedef struct {
    char version[32];               // Semantic version (e.g., "1.2.3")
    char download_url[256];         // URL to firmware binary
    char signature[128];            // Ed25519 signature (base64 or hex)
    size_t signature_len;           // Actual signature length
    size_t file_size;               // Expected firmware size
    bool is_mandatory;              // Force update flag
    char release_notes[512];        // Optional release notes
} dlm_release_info_t;

/* ============== OTA Provider Interface ==============
 * Abstract interface for firmware update sources
 * Implement this to add new update sources (firmware-manager backend, etc.)
 */

struct dlm_ota_provider;

typedef esp_err_t (*ota_query_fn_t)(
    struct dlm_ota_provider *provider,
    const char *server_config,      // URL, repo name, etc.
    const char *current_version,    // Current firmware version
    dlm_release_info_t *out_info    // Output: release info
);

typedef esp_err_t (*ota_download_fn_t)(
    struct dlm_ota_provider *provider,
    const dlm_release_info_t *info,
    void (*progress_cb)(size_t downloaded, size_t total)
);

typedef struct dlm_ota_provider {
    const char *name;               // Provider name (e.g., "github", "custom")
    ota_query_fn_t query;           // Query for available update
    ota_download_fn_t download;     // Download firmware
    void *ctx;                      // Provider-specific context
} dlm_ota_provider_t;

/* ============== HTTP Request/Response ============== */

typedef struct {
    const char *method;             // "GET", "POST", etc.
    const char *path;               // Request path
    const char *query_string;       // URL query parameters
    const char *body;               // Request body (for POST)
    size_t body_len;
    int client_fd;                  // Client socket for sending response
    
    /* Parsed form data (populated by form_parser) */
    struct {
        const char *key;
        const char *value;
    } form_data[16];                // Max 16 form fields
    size_t form_count;
} http_request_t;

typedef struct {
    int status_code;                // HTTP status
    const char *content_type;       // Content-Type header
    const char *body;               // Response body
    size_t body_len;
} http_response_t;

/* ============== HTTP Handler ============== */

typedef esp_err_t (*http_handler_fn_t)(
    const http_request_t *req,
    http_response_t *resp,
    void *user_ctx
);

/* ============== Error Codes ============== */

#define DLM_ERR_BASE                0x4000
#define DLM_ERR_NO_CREDENTIALS      (DLM_ERR_BASE + 1)
#define DLM_ERR_WIFI_CONNECT_FAIL   (DLM_ERR_BASE + 2)
#define DLM_ERR_OTA_NO_UPDATE       (DLM_ERR_BASE + 3)
#define DLM_ERR_OTA_DOWNLOAD_FAIL   (DLM_ERR_BASE + 4)
#define DLM_ERR_SIGNATURE_INVALID   (DLM_ERR_BASE + 5)
#define DLM_ERR_CONFIG_INVALID      (DLM_ERR_BASE + 6)
#define DLM_ERR_FACTORY_RESET       (DLM_ERR_BASE + 7)

/* ============== Utility Macros ============== */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define DLM_TAG "DLM"

#ifdef __cplusplus
}
#endif
