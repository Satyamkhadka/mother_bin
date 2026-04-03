/**
 * @file custom_provider.c
 * @brief Custom Server OTA Provider (PLACEHOLDER)
 * 
 * This is a PLACEHOLDER implementation for your own OTA server.
 * Implement this file to connect to your custom update server.
 * 
 * API Design (as specified by user):
 * - Server decides based on device info
 * - POST /api/check-update
 * - Body: {device_id, current_version, hardware_version, chip_model}
 * - Response: {update_available, version, download_url, signature, force_update}
 */

#include "custom_provider.h"
#include "dlm_config.h"
#include "storage/nvs_manager.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "custom_prov";

/* ============== PLACEHOLDER NOTE ==============
 * 
 * This file contains stub implementations.
 * To use your custom server:
 * 
 * 1. Implement query function to call your API
 * 2. Implement download function
 * 3. Update sdkconfig to set your server URL
 * 
 * Example API implementation is provided in comments.
 */

/* ============== Configuration ============== */

/* ============== Device ID Generation ==============
 * Uses MAC address as unique device ID
 */
static void get_device_id(char *device_id, size_t len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ============== Query Implementation (PLACEHOLDER) ==============
 * 
 * TODO: Implement your custom server query here.
 * 
 * Example implementation:
 * 
 * static esp_err_t custom_query(const char *server_config,
 *                                const char *current_version,
 *                                dlm_release_info_t *out_info)
 * {
 *     // server_config contains your server URL
 *     const char *server_url = server_config;
 *     
 *     // Build request body
 *     char device_id[32];
 *     get_device_id(device_id, sizeof(device_id));
 *     
 *     cJSON *req = cJSON_CreateObject();
 *     cJSON_AddStringToObject(req, "device_id", device_id);
 *     cJSON_AddStringToObject(req, "current_version", current_version);
 *     cJSON_AddStringToObject(req, "hardware_version", "v1.0");  // Your HW version
 *     cJSON_AddStringToObject(req, "chip_model", CONFIG_IDF_TARGET);
 *     
 *     char *post_data = cJSON_Print(req);
 *     cJSON_Delete(req);
 *     
 *     // Make HTTP POST request
 *     char url[256];
 *     snprintf(url, sizeof(url), "%s%s", server_url, 
 *              DLM_CUSTOM_SERVER_API_PATH);
 *     
 *     // ... HTTP request code ...
 *     
 *     // Parse response
 *     // Expected: {"update_available":true, "version":"1.2.3", 
 *     //            "download_url":"https://...", "signature":"base64...",
 *     //            "force_update":false}
 *     
 *     free(post_data);
 *     return ESP_OK;
 * }
 */

static esp_err_t custom_query(const char *server_config,
                               const char *current_version,
                               dlm_release_info_t *out_info)
{
    (void)server_config;
    (void)current_version;
    (void)out_info;
    
    ESP_LOGW(TAG, "Custom provider query not implemented!");
    ESP_LOGW(TAG, "Please implement custom_provider.c for your server API");
    
    /* PLACEHOLDER: Return not found so we don't break the build */
    /* In your implementation, return ESP_OK if update available */
    return ESP_ERR_NOT_FOUND;
}

/* ============== Download Implementation (PLACEHOLDER) ==============
 * 
 * TODO: Implement your firmware download here.
 * 
 * This is typically similar to the GitHub provider - use esp_https_ota.
 * You may need to add custom headers for authentication.
 */

static esp_err_t custom_download(const char *download_url,
                                  void (*progress_cb)(int percent))
{
    (void)download_url;
    (void)progress_cb;
    
    ESP_LOGW(TAG, "Custom provider download not implemented!");
    ESP_LOGW(TAG, "Please implement custom_provider.c for your server API");
    
    return ESP_ERR_NOT_SUPPORTED;
}

/* ============== Provider Instance ============== */

static const ota_provider_t s_custom_provider = {
    .name = "custom",
    .query = custom_query,
    .download = custom_download,
};

const ota_provider_t* ota_provider_get_custom(void)
{
    return &s_custom_provider;
}

/* ============== Implementation Guide ==============
 * 
 * To implement your custom provider:
 * 
 * 1. Replace custom_query() with your API call:
 *    
 *    - Get your server URL from server_config or DLM_CUSTOM_SERVER_DEFAULT_URL
 *    - Build JSON body with device_id, current_version, hardware_version
 *    - Make HTTP POST to /api/check-update
 *    - Parse response for update_available, version, download_url, signature
 *    - Fill out_info structure
 *    - Return ESP_OK if update available
 * 
 * 2. Replace custom_download() with your download logic:
 *    
 *    - Similar to GitHub provider using esp_https_ota
 *    - May need authentication headers
 *    - Call progress_cb periodically if provided
 * 
 * 3. Configure via web UI or NVS:
 *    
 *    - Set provider to "custom"
 *    - Set server_url to your API endpoint
 * 
 * 4. Example curl test:
 *    
 *    curl -X POST https://your-server.com/api/check-update \
 *      -H "Content-Type: application/json" \
 *      -d '{"device_id":"A1B2C3D4E5F6", "current_version":"1.0.0",
 *           "hardware_version":"v2", "chip_model":"esp32s3"}'
 * 
 * 5. Security considerations:
 *    
 *    - Use HTTPS with valid certificates
 *    - Consider device authentication (HMAC, client certs)
 *    - Validate server certificate
 *    - Sign firmware with Ed25519 and verify before installation
 */
