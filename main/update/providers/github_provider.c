/**
 * @file github_provider.c
 * @brief GitHub Releases OTA Provider
 * 
 * Fetches firmware updates from GitHub releases.
 * Expected release structure:
 * - firmware.bin: The firmware binary
 * - firmware.bin.sig: Ed25519 signature (base64 encoded)
 * 
 * Release tag should be semantic version (e.g., v1.2.3)
 */

#include "github_provider.h"
#include "dlm_config.h"
#include "update/ota_provider.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "github_prov";

/* ============== Configuration ============== */

#define GITHUB_API_HOST     "api.github.com"
#define GITHUB_API_URL      "https://api.github.com"
#define HTTP_BUF_SIZE       4096
#define MAX_URL_LEN         512
#define MAX_VERSION_LEN     32

/* ============== Helper Functions ============== */

/**
 * @brief Parse "owner/repo" format
 */
static bool parse_repo(const char *repo_config, char *owner, size_t owner_len,
                       char *repo, size_t repo_len)
{
    const char *slash = strchr(repo_config, '/');
    if (slash == NULL) {
        return false;
    }
    
    size_t olen = slash - repo_config;
    if (olen >= owner_len) return false;
    
    memcpy(owner, repo_config, olen);
    owner[olen] = '\0';
    
    strlcpy(repo, slash + 1, repo_len);
    
    return (strlen(repo) > 0);
}

/**
 * @brief HTTP event handler for accumulating response
 */
typedef struct {
    char *buffer;
    size_t buf_size;
    size_t data_len;
} http_accumulator_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_accumulator_t *acc = (http_accumulator_t *)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (acc && evt->data_len > 0) {
                if (acc->data_len + evt->data_len >= acc->buf_size - 1) {
                    ESP_LOGE(TAG, "HTTP buffer overflow");
                    return ESP_FAIL;
                }
                memcpy(acc->buffer + acc->data_len, evt->data, evt->data_len);
                acc->data_len += evt->data_len;
                acc->buffer[acc->data_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief Query GitHub API for latest release
 */
static esp_err_t query_github_api(const char *owner, const char *repo,
                                   char *response_buf, size_t buf_size)
{
    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s/repos/%s/%s/releases/latest",
             GITHUB_API_URL, owner, repo);
    
    ESP_LOGI(TAG, "Querying: %s", url);
    
    http_accumulator_t acc = {
        .buffer = response_buf,
        .buf_size = buf_size,
        .data_len = 0
    };
    
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &acc,
        .timeout_ms = 30000,
        .method = HTTP_METHOD_GET,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }
    
    /* Set required headers */
    esp_http_client_set_header(client, "User-Agent", DLM_GITHUB_USER_AGENT);
    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");
    
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGE(TAG, "GitHub API returned %d", status);
            ESP_LOGE(TAG, "Response: %s", response_buf);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }
    
    esp_http_client_cleanup(client);
    return err;
}

/**
 * @brief Parse release JSON and extract firmware info
 */
static esp_err_t parse_release(const char *json, dlm_release_info_t *info,
                                bool include_prerelease)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }
    
    /* Check if prerelease */
    cJSON *prerelease = cJSON_GetObjectItem(root, "prerelease");
    if (prerelease && cJSON_IsTrue(prerelease) && !include_prerelease) {
        ESP_LOGI(TAG, "Skipping prerelease");
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Get version from tag_name */
    cJSON *tag_name = cJSON_GetObjectItem(root, "tag_name");
    if (!tag_name || !cJSON_IsString(tag_name)) {
        ESP_LOGE(TAG, "Missing tag_name");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    /* Remove 'v' prefix if present */
    const char *tag = tag_name->valuestring;
    if (tag[0] == 'v' || tag[0] == 'V') {
        tag++;
    }
    strlcpy(info->version, tag, sizeof(info->version));
    
    /* Get release notes */
    cJSON *body = cJSON_GetObjectItem(root, "body");
    if (body && cJSON_IsString(body)) {
        strlcpy(info->release_notes, body->valuestring, sizeof(info->release_notes));
    }
    
    /* Find firmware.bin asset */
    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (!assets || !cJSON_IsArray(assets)) {
        ESP_LOGE(TAG, "Missing assets array");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    bool found_firmware = false;
    bool found_signature = false;
    
    cJSON *asset;
    cJSON_ArrayForEach(asset, assets) {
        cJSON *name = cJSON_GetObjectItem(asset, "name");
        cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
        
        if (!name || !cJSON_IsString(name) || !url || !cJSON_IsString(url)) {
            continue;
        }
        
        if (strcmp(name->valuestring, "firmware.bin") == 0) {
            strlcpy(info->download_url, url->valuestring, sizeof(info->download_url));
            found_firmware = true;
            
            /* Get file size */
            cJSON *size = cJSON_GetObjectItem(asset, "size");
            if (size && cJSON_IsNumber(size)) {
                info->file_size = size->valueint;
            }
        }
        else if (strcmp(name->valuestring, "firmware.bin.sig") == 0) {
            /* Download signature */
            /* NOTE: For now we assume signature is delivered with firmware
             * In production, you might want to download and cache it separately
             */
            found_signature = true;
        }
    }
    
    cJSON_Delete(root);
    
    if (!found_firmware) {
        ESP_LOGE(TAG, "firmware.bin not found in release");
        return ESP_FAIL;
    }
    
    if (!found_signature) {
        ESP_LOGW(TAG, "firmware.bin.sig not found - will need separate download");
    }
    
    return ESP_OK;
}

/* ============== Provider Implementation ============== */

static esp_err_t github_query(const char *server_config,
                               const char *current_version,
                               dlm_release_info_t *out_info)
{
    if (server_config == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char owner[64];
    char repo[64];
    
    if (!parse_repo(server_config, owner, sizeof(owner), repo, sizeof(repo))) {
        ESP_LOGE(TAG, "Invalid repo format: %s (expected owner/repo)", server_config);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Query GitHub API */
    char response[HTTP_BUF_SIZE];
    esp_err_t err = query_github_api(owner, repo, response, sizeof(response));
    if (err != ESP_OK) {
        return err;
    }
    
    /* Parse release */
    /* TODO: Get prerelease preference from config */
    bool include_prerelease = false;
    err = parse_release(response, out_info, include_prerelease);
    if (err != ESP_OK) {
        return err;
    }
    
    ESP_LOGI(TAG, "Latest version: %s", out_info->version);
    ESP_LOGI(TAG, "Download URL: %s", out_info->download_url);
    
    /* Compare versions */
    extern int ota_manager_version_compare(const char *v1, const char *v2);
    int cmp = ota_manager_version_compare(current_version, out_info->version);
    
    if (cmp >= 0) {
        ESP_LOGI(TAG, "Current version %s is up to date", current_version);
        return ESP_ERR_NOT_FOUND;  /* No update needed */
    }
    
    ESP_LOGI(TAG, "Update available: %s -> %s", current_version, out_info->version);
    return ESP_OK;
}

static esp_err_t github_download(const char *download_url,
                                  void (*progress_cb)(int percent))
{
    ESP_LOGI(TAG, "Downloading firmware from: %s", download_url);
    
    /* Use esp_https_ota for download */
    esp_http_client_config_t config = {
        .url = download_url,
        .timeout_ms = DLM_OTA_DOWNLOAD_TIMEOUT_SEC * 1000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    /* Configure OTA */
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    
    /* Perform OTA */
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA download complete");
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/* ============== Provider Instance ============== */

static const ota_provider_t s_github_provider = {
    .name = "github",
    .query = github_query,
    .download = github_download,
};

const ota_provider_t* ota_provider_get_github(void)
{
    return &s_github_provider;
}
