/**
 * @file custom_provider.c
 * @brief Firmware Manager OTA Provider
 *
 * Queries the firmware-manager backend for updates.
 *
 * API:
 *   POST /api/check-update
 *   Body: {device_id, current_version, hardware_version, chip_model}
 *   Response: {update_available, version, download_url, signature,
 *              file_size, force_update, release_notes}
 */

#include "custom_provider.h"
#include "dlm_config.h"
#include "storage/config_store.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "fm_prov";

/* ============== HTTP Accumulator ============== */

typedef struct {
    char  *buffer;
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

/* ============== Helpers ============== */

static void get_device_id(char *device_id, size_t len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ============== Query ============== */

static esp_err_t custom_query(const char *server_config,
                               const char *current_version,
                               dlm_release_info_t *out_info)
{
    if (server_config == NULL || strlen(server_config) == 0 || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char device_id[32];
    get_device_id(device_id, sizeof(device_id));

    char hardware_version[32] = "";
    config_store_get_string("hardware_version", hardware_version, sizeof(hardware_version));
    if (strlen(hardware_version) == 0) {
        strcpy(hardware_version, "v1");
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "device_id", device_id);
    cJSON_AddStringToObject(req, "current_version", current_version);
    cJSON_AddStringToObject(req, "hardware_version", hardware_version);
    cJSON_AddStringToObject(req, "chip_model", CONFIG_IDF_TARGET);
    char *post_data = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    char url[320];
    snprintf(url, sizeof(url), "%s%s", server_config, DLM_CUSTOM_SERVER_API_PATH);

    char response_buf[4096] = {0};
    http_accumulator_t acc = {
        .buffer   = response_buf,
        .buf_size = sizeof(response_buf),
        .data_len = 0
    };

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_event_handler,
        .user_data     = &acc,
        .timeout_ms    = 15000,
        .method        = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(post_data);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(post_data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Server returned %d", status);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(response_buf);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_FAIL;
    }

    cJSON *j_avail = cJSON_GetObjectItem(root, "update_available");
    if (!cJSON_IsTrue(j_avail)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *j_version      = cJSON_GetObjectItem(root, "version");
    cJSON *j_url          = cJSON_GetObjectItem(root, "download_url");
    cJSON *j_sig          = cJSON_GetObjectItem(root, "signature");

    if (!cJSON_IsString(j_version) || !cJSON_IsString(j_url) || !cJSON_IsString(j_sig)) {
        ESP_LOGE(TAG, "Missing required fields in response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strlcpy(out_info->version,      j_version->valuestring,      sizeof(out_info->version));
    strlcpy(out_info->download_url, j_url->valuestring,          sizeof(out_info->download_url));
    strlcpy(out_info->signature,    j_sig->valuestring,          sizeof(out_info->signature));

    cJSON *j_size = cJSON_GetObjectItem(root, "file_size");
    if (cJSON_IsNumber(j_size)) {
        out_info->file_size = (size_t)j_size->valueint;
    }

    cJSON *j_force = cJSON_GetObjectItem(root, "force_update");
    out_info->is_mandatory = cJSON_IsTrue(j_force);

    cJSON *j_notes = cJSON_GetObjectItem(root, "release_notes");
    if (cJSON_IsString(j_notes)) {
        strlcpy(out_info->release_notes, j_notes->valuestring,
                sizeof(out_info->release_notes));
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Update available: %s -> %s", current_version, out_info->version);
    return ESP_OK;
}

/* ============== Download ============== */

static esp_err_t custom_download(const char *download_url,
                                  void (*progress_cb)(int percent))
{
    ESP_LOGI(TAG, "Downloading firmware from: %s", download_url);

    esp_http_client_config_t cfg = {
        .url               = download_url,
        .timeout_ms        = DLM_OTA_DOWNLOAD_TIMEOUT_SEC * 1000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &cfg,
    };

    if (progress_cb) {
        progress_cb(10);
    }

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA download complete");
        if (progress_cb) {
            progress_cb(90);
        }
    } else {
        ESP_LOGE(TAG, "OTA download failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

/* ============== Device Claiming ============== */

esp_err_t custom_provider_claim_device(const char *server_url,
                                        const char *claim_token,
                                        char *node_id, size_t node_id_len,
                                        char *node_secret, size_t node_secret_len,
                                        char *required_fw_version, size_t fw_ver_len)
{
    if (server_url == NULL || strlen(server_url) == 0 ||
        claim_token == NULL || strlen(claim_token) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char device_id[32];
    get_device_id(device_id, sizeof(device_id));

    char hardware_version[32] = "";
    config_store_get_string("hardware_version", hardware_version, sizeof(hardware_version));
    if (strlen(hardware_version) == 0) {
        strcpy(hardware_version, "v1");
    }

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "claim_token", claim_token);
    cJSON_AddStringToObject(req, "device_id", device_id);
    cJSON_AddStringToObject(req, "hardware_version", hardware_version);
    cJSON_AddStringToObject(req, "chip_model", CONFIG_IDF_TARGET);
    char *post_data = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);

    char url[320];
    snprintf(url, sizeof(url), "%s%s", server_url, DLM_CUSTOM_SERVER_CLAIM_PATH);

    char response_buf[4096] = {0};
    http_accumulator_t acc = {
        .buffer   = response_buf,
        .buf_size = sizeof(response_buf),
        .data_len = 0
    };

    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_event_handler,
        .user_data     = &acc,
        .timeout_ms    = 15000,
        .method        = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(post_data);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(post_data);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Claim request failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Claim server returned %d", status);
        return ESP_FAIL;
    }
        ESP_LOGI(TAG, "Claim server returned %d", status);


    cJSON *root = cJSON_Parse(response_buf);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse claim response");
        return ESP_FAIL;
    }

    cJSON *j_success = cJSON_GetObjectItem(root, "success");
    if (!cJSON_IsTrue(j_success)) {
        ESP_LOGE(TAG, "Sucess is not true");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *j_node_id = cJSON_GetObjectItem(root, "node_id");
    cJSON *j_secret  = cJSON_GetObjectItem(root, "node_secret");

    if (!cJSON_IsString(j_node_id) || !cJSON_IsString(j_secret)) {
        ESP_LOGE(TAG, "Missing node_id or node_secret in claim response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    strlcpy(node_id, j_node_id->valuestring, node_id_len);
    strlcpy(node_secret, j_secret->valuestring, node_secret_len);

    cJSON *j_fw = cJSON_GetObjectItem(root, "required_fw_version");
    if (cJSON_IsString(j_fw) && required_fw_version != NULL && fw_ver_len > 0) {
        strlcpy(required_fw_version, j_fw->valuestring, fw_ver_len);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Device claimed: node_id=%s", node_id);
    return ESP_OK;
}

/* ============== Provider Instance ============== */

static const ota_provider_t s_custom_provider = {
    .name     = "custom",
    .query    = custom_query,
    .download = custom_download,
};

const ota_provider_t* ota_provider_get_custom(void)
{
    return &s_custom_provider;
}
