/**
 * @file wifi_manager.h
 * @brief WiFi connection management (STA + AP modes)
 * 
 * Supports:
 * - Station mode: Connect to configured WiFi network
 * - AP mode: Start provisioning access point
 * - Concurrent mode: AP + STA simultaneously during provisioning
 */

#pragma once

#include "dlm_types.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== WiFi Credentials Storage ============== */

/**
 * @brief Maximum SSID length (32 + null terminator)
 */
#define WIFI_MAX_SSID_LEN   33

/**
 * @brief Maximum password length (64 + null terminator)
 */
#define WIFI_MAX_PASS_LEN   65

/* ============== Initialization ============== */

/**
 * @brief Initialize WiFi manager
 * 
 * Initializes WiFi driver, event loop, and netif.
 * Does NOT start WiFi - call wifi_manager_start_ap() or wifi_manager_connect().
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Deinitialize WiFi manager
 * 
 * Stops WiFi and frees resources.
 */
void wifi_manager_deinit(void);

/* ============== Credential Management ============== */

/**
 * @brief Check if WiFi credentials are stored
 * 
 * @return true if SSID is stored in NVS
 */
bool wifi_manager_has_credentials(void);

/**
 * @brief Get stored WiFi credentials
 * 
 * @param ssid      Output buffer for SSID (min 33 bytes)
 * @param ssid_len  Buffer size
 * @param pass      Output buffer for password (min 65 bytes)
 * @param pass_len  Buffer size
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no credentials
 */
esp_err_t wifi_manager_get_credentials(char *ssid, size_t ssid_len,
                                        char *pass, size_t pass_len);

/**
 * @brief Save WiFi credentials to NVS
 * 
 * @param ssid      WiFi SSID
 * @param password  WiFi password (empty string for open networks)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/**
 * @brief Erase stored WiFi credentials
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_erase_credentials(void);

/* ============== Connection Management ============== */

/**
 * @brief Connect to WiFi network
 * 
 * Uses stored credentials. Call wifi_manager_save_credentials() first.
 * This is non-blocking - connection happens in background.
 * Check wifi_manager_is_connected() or wait for callback.
 * 
 * @return ESP_OK if connection started
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect from WiFi network
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Check if currently connected to WiFi
 * 
 * @return true if connected and has IP address
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Get connection state
 * 
 * @return Current WiFi state
 */
dlm_wifi_state_t wifi_manager_get_state(void);

/**
 * @brief Wait for connection with timeout
 * 
 * Blocking call that waits for connection or timeout.
 * 
 * @param timeout_ms    Timeout in milliseconds
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT if timeout
 */
esp_err_t wifi_manager_wait_for_connection(uint32_t timeout_ms);

/* ============== AP Mode (Provisioning) ============== */

/**
 * @brief Start AP mode for provisioning
 * 
 * Creates AP with SSID: ELM-<MAC_ADDRESS>
 * Uses DHCP server to assign IPs to connected clients.
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * @brief Stop AP mode
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_stop_ap(void);

/**
 * @brief Check if AP mode is active
 * 
 * @return true if AP is running
 */
bool wifi_manager_is_ap_active(void);

/**
 * @brief Get number of stations connected to AP
 * 
 * @return Number of connected stations
 */
int wifi_manager_get_ap_sta_count(void);

/* ============== WiFi Scan ============== */

/**
 * @brief WiFi scan result entry
 */
typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth_mode;  /* wifi_auth_mode_t */
    bool is_hidden;
} wifi_scan_result_t;

/**
 * @brief Start WiFi scan for available networks
 * 
 * This is a blocking call that performs a scan.
 * Results are stored internally.
 * 
 * @param hidden    Include hidden networks in scan
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_scan(bool hidden);

/**
 * @brief Get scan results
 * 
 * Returns pointer to internal scan results array.
 * Do not modify or free. Valid until next scan.
 * 
 * @param results   Output pointer to results array
 * @param count     Output number of results
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_scan_results(const wifi_scan_result_t **results, 
                                         size_t *count);

/* ============== IP Information ============== */

/**
 * @brief Get IP address when in STA mode
 * 
 * @param ip_addr   Output buffer (min 16 bytes for "xxx.xxx.xxx.xxx")
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ip(char *ip_addr, size_t len);

/**
 * @brief Get AP IP address
 * 
 * @param ip_addr   Output buffer
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ap_ip(char *ip_addr, size_t len);

/* ============== Event Callback Registration ==============
 * The boot manager registers callbacks to be notified of state changes
 */

/**
 * @brief Callback for WiFi connected event
 * 
 * Called when station gets IP address.
 */
void boot_manager_on_wifi_connected(void);

/**
 * @brief Callback for WiFi disconnected/failed event
 */
void boot_manager_on_wifi_failed(void);

#ifdef __cplusplus
}
#endif
