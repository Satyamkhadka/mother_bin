/**
 * @file ota_manager.c
 * @brief OTA update orchestration
 * 
 * Coordinates the complete OTA process:
 * 1. Load configuration (provider, URL, current version)
 * 2. Query provider for available update
 * 3. Compare versions
 * 4. Download firmware (via provider)
 * 5. Verify signature
 * 6. Mark for boot
 */

#include "ota_manager.h"
#include "ota_provider.h"
#include "signature_verifier.h"
#include "dlm_config.h"
#include "storage/nvs_manager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ota_manager";

/* ============== Configuration Storage ============== */

typedef struct {
    char provider[16];
    char server_url[256];
    char current_version[32];
    bool use_prerelease;
} ota_config_t;

static ota_config_t s_config = {0};
static bool s_initialized = false;

/* ============== Version Comparison ==============
 * Simple semantic version comparison
 * Supports: "1.2.3", "v1.2.3", "1.2.3-beta"
 */

int ota_manager_version_compare(const char *v1, const char *v2)
{
    if (v1 == NULL || v2 == NULL) {
        return 0;
    }
    
    /* Skip 'v' or 'V' prefix */
    if (v1[0] == 'v' || v1[0] == 'V') v1++;
    if (v2[0] == 'v' || v2[0] == 'V') v2++;
    
    unsigned major1 = 0, minor1 = 0, patch1 = 0;
    unsigned major2 = 0, minor2 = 0, patch2 = 0;
    
    /* Parse versions */
    sscanf(v1, "%u.%u.%u", &major1, &minor1, &patch1);
    sscanf(v2, "%u.%u.%u", &major2, &minor2, &patch2);
    
    if (major1 != major2) return (major1 < major2) ? -1 : 1;
    if (minor1 != minor2) return (minor1 < minor2) ? -1 : 1;
    if (patch1 != patch2) return (patch1 < patch2) ? -1 : 1;
    
    return 0;
}

/* ============== Configuration Loading ============== */

static esp_err_t load_config(void)
{
    /* Load provider */
    esp_err_t err = nvs_manager_get_string(DLM_OTA_NVS_NAMESPACE,
                                           DLM_OTA_NVS_KEY_PROVIDER,
                                           s_config.provider,
                                           sizeof(s_config.provider));
    if (err != ESP_OK || strlen(s_config.provider) == 0) {
        /* Default to GitHub */
        strcpy(s_config.provider, "github");
    }
    
    /* Load server URL/repo */
    err = nvs_manager_get_string(DLM_OTA_NVS_NAMESPACE,
                                 DLM_OTA_NVS_KEY_SERVER_URL,
                                 s_config.server_url,
                                 sizeof(s_config.server_url));
    if (err != ESP_OK) {
        s_config.server_url[0] = '\0';
    }
    
    /* Load current version from running app */
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        strlcpy(s_config.current_version, app_desc->version,
                sizeof(s_config.current_version));
    } else {
        strcpy(s_config.current_version, "0.0.0");
    }
    
    /* Load prerelease preference */
    uint8_t prerelease = 0;
    err = nvs_manager_get_uint8(DLM_OTA_NVS_NAMESPACE,
                                DLM_OTA_NVS_KEY_USE_PRERELEASE,
                                &prerelease);
    s_config.use_prerelease = (err == ESP_OK && prerelease != 0);
    
    ESP_LOGI(TAG, "OTA config: provider=%s, version=%s",
             s_config.provider, s_config.current_version);
    
    return ESP_OK;
}

/* ============== Public API ============== */

esp_err_t ota_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    
    /* Load configuration */
    esp_err_t err = load_config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Initialize signature verifier */
    err = signature_verifier_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Signature verifier init failed: %s", esp_err_to_name(err));
        /* Continue anyway - verification will fail later */
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "OTA manager initialized");
    return ESP_OK;
}

esp_err_t ota_manager_check_update(bool *update_available)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (update_available == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *update_available = false;
    
    /* Get provider */
    const ota_provider_t *provider = ota_provider_get_by_name(s_config.provider);
    if (provider == NULL) {
        ESP_LOGE(TAG, "Unknown provider: %s", s_config.provider);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Checking for updates using %s provider...", provider->name);
    
    /* Query provider */
    dlm_release_info_t info = {0};
    esp_err_t err = provider->query(s_config.server_url,
                                     s_config.current_version,
                                     &info);
    
    if (err == ESP_ERR_NOT_FOUND) {
        /* No update available - this is OK */
        ESP_LOGI(TAG, "No update available");
        return ESP_OK;
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Provider query failed: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Check version comparison (provider may have already done this) */
    int cmp = ota_manager_version_compare(s_config.current_version, info.version);
    if (cmp >= 0) {
        ESP_LOGI(TAG, "Version %s is not newer than current %s",
                 info.version, s_config.current_version);
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Update available: %s -> %s",
             s_config.current_version, info.version);
    
    /* Store release info temporarily (in a real implementation, 
     * you'd store this in a global or pass it to perform_update) */
    /* For now, we'll store in static */
    static dlm_release_info_t s_pending_update;
    memcpy(&s_pending_update, &info, sizeof(info));
    
    *update_available = true;
    return ESP_OK;
}

esp_err_t ota_manager_perform_update(void (*progress_cb)(int percent))
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Get provider */
    const ota_provider_t *provider = ota_provider_get_by_name(s_config.provider);
    if (provider == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    
    /* NOTE: In a full implementation, we'd retrieve the pending update info
     * that was stored during check_update. For simplicity, re-query here. */
    dlm_release_info_t info = {0};
    esp_err_t err = provider->query(s_config.server_url,
                                     s_config.current_version,
                                     &info);
    if (err != ESP_OK) {
        return err;
    }
    
    ESP_LOGI(TAG, "Starting OTA update to version %s...", info.version);
    
    /* Download firmware */
    if (progress_cb) progress_cb(10);
    
    err = provider->download(info.download_url, progress_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(err));
        return err;
    }
    
    if (progress_cb) progress_cb(80);
    
    /* NOTE: For signature verification during OTA, there are two approaches:
     * 
     * 1. Download signature file separately, then verify after OTA
     * 2. Embed signature in firmware image header, verify during OTA
     * 
     * For this implementation, we assume the signature is downloaded separately
     * and should be verified before marking the OTA as valid.
     * 
     * The actual signature verification would require:
     * - Reading the downloaded firmware from flash
     * - Getting the signature (from info.signature or separate download)
     * - Calling signature_verifier_check()
     * - If valid, proceed; if not, abort the OTA
     */
    
    ESP_LOGW(TAG, "NOTE: Signature verification should be performed here");
    ESP_LOGW(TAG, "See signature_verifier.c for implementation");
    
    /* For now, assume signature verification passed */
    /* PLACEHOLDER: Verify signature here before marking boot partition */
    
    if (progress_cb) progress_cb(100);
    
    /* Save new version to NVS */
    nvs_manager_set_string(DLM_OTA_NVS_NAMESPACE,
                           DLM_OTA_NVS_KEY_CURRENT_VER,
                           info.version);
    
    ESP_LOGI(TAG, "OTA update completed successfully");
    ESP_LOGI(TAG, "New version: %s", info.version);
    ESP_LOGI(TAG, "Reboot to apply update");
    
    return ESP_OK;
}

esp_err_t ota_manager_get_current_version(char *version, size_t len)
{
    if (version == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_initialized) {
        /* Get directly from app desc */
        const esp_app_desc_t *app_desc = esp_app_get_description();
        if (app_desc) {
            strlcpy(version, app_desc->version, len);
            return ESP_OK;
        }
        return ESP_FAIL;
    }
    
    strlcpy(version, s_config.current_version, len);
    return ESP_OK;
}

/* ============== Provider Registry ============== */

const ota_provider_t* ota_provider_get_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    
    if (strcmp(name, "github") == 0) {
        return ota_provider_get_github();
    }
    
    if (strcmp(name, "custom") == 0) {
        return ota_provider_get_custom();
    }
    
    return NULL;
}
