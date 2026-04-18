/**
 * @file dlm_config.h
 * @brief Compile-time configuration for Device Lifecycle Manager
 * 
 * Modify these values to customize DLM behavior for your device.
 * All configuration here is determined at compile time.
 */

#pragma once

#include "dlm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Device Identity ============== */

/** 
 * SSID prefix for provisioning mode AP
 * Full SSID will be: ELM-<FullMAC> (e.g., ELM-A1B2C3D4E5F6)
 */
#define DLM_AP_SSID_PREFIX          "BASE"

/** 
 * AP password (NULL for open network, or min 8 chars for WPA2)
 * NULL = open network (convenient but insecure)
 */
#define DLM_AP_PASSWORD             NULL

/** AP channel (1-14, 1, 6, 11 are non-overlapping) */
#define DLM_AP_CHANNEL              1

/** Maximum simultaneous STA connections to AP */
#define DLM_AP_MAX_CONNECTIONS      4

/* ============== Network Configuration ============== */

/** AP mode IP configuration */
#define DLM_AP_IP_ADDR              "192.168.4.1"
#define DLM_AP_NETMASK              "255.255.255.0"
#define DLM_AP_GATEWAY              "192.168.4.1"

/** DNS server port (for captive portal) */
#define DLM_DNS_PORT                53

/** HTTP server port */
#define DLM_HTTP_PORT               80

/** HTTP server task configuration */
#define DLM_HTTP_TASK_STACK_SIZE    32768
#define DLM_HTTP_TASK_PRIORITY      5
#define DLM_HTTP_MAX_CLIENTS        3

/* ============== WiFi Configuration ============== */

/** WiFi connection timeout (seconds) */
#define DLM_WIFI_CONNECT_TIMEOUT_SEC    60

/** Maximum STA connection retries before falling back to provisioning */
#define DLM_WIFI_MAX_CONNECT_RETRIES    5

/** Maximum WiFi scan results to store/display */
#define DLM_WIFI_MAX_SCAN_RESULTS       20

/** NVS namespace for WiFi credentials */
#define DLM_WIFI_NVS_NAMESPACE          "wifi_creds"
#define DLM_WIFI_NVS_KEY_SSID           "ssid"
#define DLM_WIFI_NVS_KEY_PASS           "password"

/* ============== Hardware Configuration ============== */

/** 
 * GPIO pin for status LED
 * Set to -1 to disable LED support
 */
#define DLM_LED_GPIO                    CONFIG_DLM_LED_GPIO

/** LED active level: 1 = active HIGH, 0 = active LOW */
#define DLM_LED_ACTIVE_HIGH             1

/** 
 * GPIO pin for factory reset button
 * Press during boot to trigger factory reset
 * Set to -1 to disable button support
 */
#define DLM_RESET_BUTTON_GPIO           CONFIG_DLM_RESET_BUTTON_GPIO

/** Button active level: 0 = active LOW (typical for pull-up), 1 = active HIGH */
#define DLM_RESET_BUTTON_ACTIVE_LOW     0

/** 
 * Button debounce time (milliseconds)
 * Must hold button for this long to trigger reset
 */
#define DLM_RESET_BUTTON_DEBOUNCE_MS    100

/* ============== Factory Reset Configuration ============== */

/** 
 * Number of consecutive resets required to trigger factory reset
 * All resets count: power cycles, software reboots, watchdog, etc.
 */
#define DLM_FACTORY_RESET_THRESHOLD     10

/** 
 * Maximum resets allowed in window (safety margin)
 * Prevents accidental reset if counter gets corrupted
 */
#define DLM_FACTORY_RESET_MAX           15

/** 
 * Time window for counting resets (milliseconds)
 * Reset counter is cleared after this time from first reset
 */
#define DLM_FACTORY_RESET_WINDOW_MS     10000  // 10 seconds

/** NVS keys for reset detection */
#define DLM_RESET_NVS_NAMESPACE         "dlm_reset"
#define DLM_RESET_NVS_KEY_COUNT         "reset_count"
#define DLM_RESET_NVS_KEY_FIRST_TIME    "first_reset_time"

/* ============== OTA Configuration ============== */

/** 
 * Maximum firmware size to accept (bytes)
 * Should match your partition size
 */
#define DLM_OTA_MAX_FIRMWARE_SIZE       (1024 * 1024)  // 1MB

/** 
 * OTA download chunk size
 * Larger = faster but more RAM
 */
#define DLM_OTA_CHUNK_SIZE              4096

/** OTA download timeout (seconds) */
#define DLM_OTA_DOWNLOAD_TIMEOUT_SEC    300

/** 
 * NVS namespace for OTA configuration
 * Stores: update server URL, current version, etc.
 */
#define DLM_OTA_NVS_NAMESPACE           "ota_config"
#define DLM_OTA_NVS_KEY_SERVER_URL      "server_url"    // backend base URL
#define DLM_OTA_NVS_KEY_CURRENT_VER     "version"
#define DLM_OTA_NVS_KEY_UPDATE_PENDING  "update_pending"

/* ============== Custom Server API (your future server) ==============
 * These defaults match the "server decides" API you specified
 */

/** Default custom server URL (override at runtime via config) */
#define DLM_CUSTOM_SERVER_DEFAULT_URL   ""

/** API endpoint path for update checks */
#define DLM_CUSTOM_SERVER_API_PATH      "/api/check-update"

/** API endpoint path for device claiming/provisioning */
#define DLM_CUSTOM_SERVER_CLAIM_PATH    "/api/provision"

/** mDNS hostname for local access after provisioning */
#define DLM_MDNS_HOSTNAME               "esplifemanager"

/* ============== Configuration Schema ==============
 * Define your device's configuration fields here.
 * These appear automatically in the web UI.
 * 
 * To add custom fields, edit the dlm_config_schema array below.
 */

#define DLM_CONFIG_NVS_NAMESPACE        "device_cfg"

/** 
 * Default configuration schema
 * Users can override this at runtime via the web UI
 */
extern const config_field_def_t dlm_config_schema[];
extern const size_t dlm_config_schema_count;

/* ============== SNTP / Time Sync ============== */

/** SNTP server for time synchronization (required for TLS) */
#define DLM_SNTP_SERVER_1               "pool.ntp.org"
#define DLM_SNTP_SERVER_2               "time.google.com"

/** Time sync timeout (seconds) */
#define DLM_SNTP_TIMEOUT_SEC            10

/* ============== Application Firmware Boot ============== */

/** 
 * Delay before booting application (milliseconds)
 * Allows user to see final status LED pattern
 */
#define DLM_BOOT_DELAY_MS               1000

/** NVS key for boot reason (app can read this) */
#define DLM_BOOT_REASON_NVS_KEY         "boot_reason"

/** Boot reason values (written to NVS for app to read) */
#define DLM_BOOT_REASON_NORMAL          "normal"
#define DLM_BOOT_REASON_AFTER_UPDATE    "after_update"
#define DLM_BOOT_REASON_FACTORY_RESET   "factory_reset"
#define DLM_BOOT_REASON_FIRST_BOOT      "first_boot"

/* ============== Debug / Development ============== */

/** Enable detailed OTA logging */
#define DLM_DEBUG_OTA                   0

/** Enable HTTP request/response logging */
#define DLM_DEBUG_HTTP                  0

/** 
 * Skip version comparison (for testing)
 * Set to 1 to force update even if versions match
 */
#define DLM_DEBUG_FORCE_UPDATE          0

#ifdef __cplusplus
}
#endif
