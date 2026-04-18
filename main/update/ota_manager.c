/**
 * @file ota_manager.c
 * @brief OTA update orchestration
 *
 * Coordinates the complete OTA process:
 * 1. Load configuration (server URL, current version)
 * 2. Query firmware-manager backend for available update
 * 3. Download firmware
 * 4. Verify Ed25519 signature
 * 5. Mark for boot
 */

#include "ota_manager.h"
#include "ota_provider.h"
#include "signature_verifier.h"
#include "dlm_config.h"
#include "storage/nvs_manager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "mbedtls/base64.h"
#include <string.h>

static const char *TAG = "ota_manager";

/* ============== Configuration Storage ============== */

typedef struct {
    char server_url[256];
    char current_version[32];
} ota_config_t;

static ota_config_t s_config = {0};
static bool s_initialized = false;
static dlm_release_info_t s_pending_update = {0};

/* ============== Configuration Loading ============== */

static esp_err_t load_config(void)
{
    esp_err_t err = nvs_manager_get_string(DLM_OTA_NVS_NAMESPACE,
                                           DLM_OTA_NVS_KEY_SERVER_URL,
                                           s_config.server_url,
                                           sizeof(s_config.server_url));
    if (err != ESP_OK) {
        s_config.server_url[0] = '\0';
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        strlcpy(s_config.current_version, app_desc->version,
                sizeof(s_config.current_version));
    } else {
        strcpy(s_config.current_version, "0.0.0");
    }

    ESP_LOGI(TAG, "OTA config: server=%s, version=%s",
             s_config.server_url, s_config.current_version);

    return ESP_OK;
}

/* ============== Public API ============== */

esp_err_t ota_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = load_config();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config: %s", esp_err_to_name(err));
        return err;
    }

    err = signature_verifier_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Signature verifier init failed: %s", esp_err_to_name(err));
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

    const ota_provider_t *provider = ota_provider_get_custom();
    if (provider == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (s_config.server_url[0] == '\0') {
        ESP_LOGW(TAG, "No OTA server configured, skipping update check");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Checking for updates...");

    dlm_release_info_t info = {0};
    esp_err_t err = provider->query(s_config.server_url,
                                     s_config.current_version,
                                     &info);

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No update available");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Query failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Update available: %s -> %s",
             s_config.current_version, info.version);

    s_pending_update = info;
    *update_available = true;
    return ESP_OK;
}

esp_err_t ota_manager_perform_update(void (*progress_cb)(int percent))
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const ota_provider_t *provider = ota_provider_get_custom();
    if (provider == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    dlm_release_info_t info = s_pending_update;
    if (strlen(info.download_url) == 0) {
        ESP_LOGE(TAG, "No pending update");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting OTA update to version %s...", info.version);

    if (progress_cb) {
        progress_cb(5);
    }

    /* Download firmware */
    esp_err_t err = provider->download(info.download_url, progress_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(err));
        return err;
    }

    if (progress_cb) {
        progress_cb(85);
    }

    /* Verify signature */
    if (info.file_size > 0 && strlen(info.signature) > 0) {
        const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
        if (ota_partition != NULL) {
            const void *mapped = NULL;
            esp_partition_mmap_handle_t mmap_handle;
            err = esp_partition_mmap(ota_partition, 0, info.file_size,
                                      ESP_PARTITION_MMAP_DATA,
                                      &mapped, &mmap_handle);
            if (err == ESP_OK) {
                uint8_t decoded_sig[ED25519_SIGNATURE_SIZE];
                size_t sig_len = 0;
                int b64_err = mbedtls_base64_decode(
                    decoded_sig, sizeof(decoded_sig), &sig_len,
                    (const uint8_t *)info.signature, strlen(info.signature));

                if (b64_err == 0 && sig_len == ED25519_SIGNATURE_SIZE) {
                    err = signature_verifier_check((const uint8_t *)mapped,
                                                    info.file_size,
                                                    decoded_sig, sig_len);
                } else {
                    ESP_LOGE(TAG, "Failed to decode signature");
                    err = ESP_FAIL;
                }

                esp_partition_munmap(mmap_handle);

                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Signature verification FAILED");

                    /* Rollback boot partition to current app */
                    const esp_partition_t *running = esp_ota_get_running_partition();
                    esp_ota_set_boot_partition(running);

                    return ESP_FAIL;
                }
                ESP_LOGI(TAG, "Signature verified");
            } else {
                ESP_LOGW(TAG, "Failed to mmap partition for verification");
            }
        }
    } else {
        ESP_LOGW(TAG, "Skipping signature verification: missing file_size or signature");
    }

    if (progress_cb) {
        progress_cb(100);
    }

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
    (void)name;
    return ota_provider_get_custom();
}
