/**
 * @file web_portal.c
 * @brief Web portal HTTP handlers implementation
 */

#include "web_portal.h"
#include "web_assets/portal_page.h"
#include "form_parser.h"
#include "dlm_config.h"
#include "storage/config_store.h"
#include "storage/nvs_manager.h"
#include "network/wifi_manager.h"
#include "network/http_server.h"
#include "network/dns_server.h"
#include "core/boot_manager.h"
#include "update/providers/custom_provider.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "web_portal";

/* ============== JSON Response Helpers ============== */

static void send_json_response(int client_fd, int status_code, const char *json)
{
    http_server_send_response(client_fd, status_code, HTTP_CONTENT_JSON, json, 0);
}

static void send_json_error(int client_fd, int status_code, const char *message)
{
    char json[256];
    snprintf(json, sizeof(json), "{\"success\":false,\"error\":\"%s\"}", message);
    send_json_response(client_fd, status_code, json);
}

static void send_json_success(int client_fd, const char *extra)
{
    if (extra && strlen(extra) > 0) {
        char json[512];
        snprintf(json, sizeof(json), "{\"success\":true,%s}", extra);
        send_json_response(client_fd, 200, json);
    } else {
        send_json_response(client_fd, 200, "{\"success\":true}");
    }
}

/* ============== HTTP Handlers ============== */

/**
 * @brief GET /config - Return configuration schema as JSON
 */
static esp_err_t handle_get_config(const http_request_t *req, 
                                    http_response_t *resp, 
                                    void *user_ctx)
{
    (void)resp; (void)user_ctx;
    
    /* Need client_fd to send response - get it from a global or modify API */
    /* For now, this handler just validates - response sent separately */
    /* Actually, let me modify the approach - use the response mechanism */
    
    /* This is called from within handle_client which has client_fd */
    /* We need to store client_fd somewhere accessible or pass it differently */
    
    return ESP_OK;
}

/**
 * @brief GET /scan - Return WiFi scan results as JSON
 */
static esp_err_t handle_get_scan(const http_request_t *req,
                                  http_response_t *resp,
                                  void *user_ctx)
{
    (void)req; (void)resp; (void)user_ctx;
    return ESP_OK;
}

/**
 * @brief POST /settings - Save configuration
 */
static esp_err_t handle_post_settings(const http_request_t *req,
                                       http_response_t *resp,
                                       void *user_ctx)
{
    (void)resp; (void)user_ctx;
    return ESP_OK;
}

/* ============== Simplified Response Mechanism ==============
 * Since the handler API doesn't pass client_fd, we'll use
 * a global or modify the approach. Let me create a simpler
 * handler registration that can access the socket.
 * 
 * For now, let me implement the handlers with a global context.
 */

typedef struct {
    int client_fd;
    const http_request_t *req;
} handler_ctx_t;

static handler_ctx_t s_handler_ctx;

#define RESPONSE_BUF_SIZE   4096

/* ============== /config Handler ============== */

static esp_err_t config_handler(int client_fd, const http_request_t *req)
{
    (void)req;
    
    char json[2048];
    size_t len = config_store_get_schema_json(json, sizeof(json));
    
    if (len == 0) {
        send_json_error(client_fd, 500, "Failed to generate config");
        return ESP_FAIL;
    }
    
    send_json_response(client_fd, 200, json);
    return ESP_OK;
}

/* ============== /scan Handler ============== */

static esp_err_t scan_handler(int client_fd, const http_request_t *req)
{
    (void)req;
    
    /* Perform scan */
    esp_err_t err = wifi_manager_scan(true);
    if (err != ESP_OK) {
        send_json_error(client_fd, 500, "Scan failed");
        return ESP_FAIL;
    }
    
    /* Get results */
    const wifi_scan_result_t *networks;
    size_t count;
    wifi_manager_get_scan_results(&networks, &count);
    
    /* Build JSON response */
    char json[RESPONSE_BUF_SIZE];
    size_t pos = 0;
    
    pos += snprintf(json + pos, sizeof(json) - pos, "{\"networks\":[");
    
    for (size_t i = 0; i < count; i++) {
        pos += snprintf(json + pos, sizeof(json) - pos,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%d,\"auth\":%d}",
            (i > 0) ? "," : "",
            networks[i].ssid,
            networks[i].rssi,
            networks[i].channel,
            networks[i].auth_mode
        );
        
        if (pos >= sizeof(json) - 100) {
            break;  /* Buffer full */
        }
    }
    
    pos += snprintf(json + pos, sizeof(json) - pos, "]}");
    
    send_json_response(client_fd, 200, json);
    return ESP_OK;
}

/* ============== /settings Handler ============== */

static esp_err_t settings_handler(int client_fd, const http_request_t *req)
{
    form_field_t fields[FORM_MAX_FIELDS];
    int field_count = form_parser_parse(req->body, req->body_len,
                                         fields, FORM_MAX_FIELDS);

    if (field_count < 0) {
        send_json_error(client_fd, 400, "Invalid form data");
        return ESP_FAIL;
    }

    /* Handle manual node config update (no WiFi fields) */
    const char *node_id = form_parser_get_value(fields, field_count, "node_id");
    const char *node_secret = form_parser_get_value(fields, field_count, "node_secret");

    if (node_id != NULL || node_secret != NULL) {
        if (node_id != NULL) {
            config_store_set_string("node_id", node_id);
        }
        if (node_secret != NULL) {
            config_store_set_string("node_secret", node_secret);
        }
        send_json_success(client_fd, NULL);
        return ESP_OK;
    }

    /* WiFi provisioning */
    const char *ssid = form_parser_get_value(fields, field_count, "hidden_ssid");
    if (ssid == NULL || strlen(ssid) == 0) {
        ssid = form_parser_get_value(fields, field_count, "selected_ssid");
    }
    const char *password = form_parser_get_value(fields, field_count, "password");
    if (password == NULL) password = "";

    if (ssid == NULL || strlen(ssid) == 0) {
        send_json_error(client_fd, 400, "SSID is required");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Configuration received for SSID: %s", ssid);

    /* Save custom configuration fields */
    for (int i = 0; i < field_count; i++) {
        if (strcmp(fields[i].key, "ssid") == 0 ||
            strcmp(fields[i].key, "password") == 0 ||
            strcmp(fields[i].key, "hidden_ssid") == 0 ||
            strcmp(fields[i].key, "hiddenNetwork") == 0) {
            continue;
        }

        if (strcmp(fields[i].key, "server_address") == 0) {
            if (strlen(fields[i].value) > 0) {
                config_store_set_string("backend_url", fields[i].value);
            }
            continue;
        }

        if (strcmp(fields[i].key, "backend_url") == 0 && strlen(fields[i].value) == 0) {
            continue;
        }

        if (config_store_set_string(fields[i].key, fields[i].value) == ESP_OK) {
            continue;
        }

        if (strcmp(fields[i].value, "true") == 0) {
            config_store_set_bool(fields[i].key, true);
        } else if (strcmp(fields[i].value, "false") == 0) {
            config_store_set_bool(fields[i].key, false);
        }

        char *endptr;
        long num = strtol(fields[i].value, &endptr, 10);
        if (*endptr == '\0') {
            config_store_set_int(fields[i].key, (int32_t)num);
        }
    }

    send_json_success(client_fd, NULL);

    vTaskDelay(pdMS_TO_TICKS(500));
    boot_manager_provisioning_complete(ssid, password);

    return ESP_OK;
}

/* ============== /status Handler ============== */

static esp_err_t status_handler(int client_fd, const http_request_t *req)
{
    (void)req;
    
    char json[1024];
    char ip[16] = "";
    char ap_ip[16] = "";
    char ssid[33] = "";
    char pass[65] = "";
    char node_id[64] = "";
    char backend_url[128] = "";
    char device_id[32] = "";
    
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    wifi_manager_get_ip(ip, sizeof(ip));
    wifi_manager_get_ap_ip(ap_ip, sizeof(ap_ip));
    wifi_manager_get_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    config_store_get_string("node_id", node_id, sizeof(node_id));
    config_store_get_string("backend_url", backend_url, sizeof(backend_url));
    
    bool wifi_connected = wifi_manager_is_connected();
    bool has_node = (strlen(node_id) > 0);
    bool ap_active = wifi_manager_is_ap_active();
    
    snprintf(json, sizeof(json),
        "{"
        "\"phase\":\"%s\","
        "\"wifi_connected\":%s,"
        "\"has_node_config\":%s,"
        "\"ap_active\":%s,"
        "\"device_id\":\"%s\","
        "\"ip\":\"%s\","
        "\"ap_ip\":\"%s\","
        "\"ssid\":\"%s\","
        "\"backend_url\":\"%s\","
        "\"node_id\":\"%s\""
        "}",
        boot_manager_get_phase_str(),
        wifi_connected ? "true" : "false",
        has_node ? "true" : "false",
        ap_active ? "true" : "false",
        device_id,
        ip,
        ap_ip,
        ssid,
        backend_url,
        node_id
    );
    
    send_json_response(client_fd, 200, json);
    return ESP_OK;
}

/* ============== /claim Handler ============== */

static esp_err_t claim_handler(int client_fd, const http_request_t *req)
{
    form_field_t fields[FORM_MAX_FIELDS];
    int field_count = form_parser_parse(req->body, req->body_len, fields, FORM_MAX_FIELDS);
    
    if (field_count < 0) {
        send_json_error(client_fd, 400, "Invalid form data");
        return ESP_FAIL;
    }
    
    const char *claim_token = form_parser_get_value(fields, field_count, "claim_token");
    if (claim_token == NULL || strlen(claim_token) == 0) {
        send_json_error(client_fd, 400, "Claim token required");
        return ESP_FAIL;
    }
    
    char server_url[256] = "";
    config_store_get_string("backend_url", server_url, sizeof(server_url));
    if (strlen(server_url) == 0) {
        send_json_error(client_fd, 400, "Server URL not configured");
        return ESP_FAIL;
    }
    
    char node_id[64] = {0};
    char node_secret[128] = {0};
    char required_fw[32] = {0};
    
    esp_err_t err = custom_provider_claim_device(server_url, claim_token,
                                                  node_id, sizeof(node_id),
                                                  node_secret, sizeof(node_secret),
                                                  required_fw, sizeof(required_fw));
    if (err != ESP_OK) {
        send_json_error(client_fd, 400, "Claim failed. Check token and server.");
        return ESP_FAIL;
    }
    
    config_store_set_string("node_id", node_id);
    config_store_set_string("node_secret", node_secret);
    config_store_set_string("claim_token", claim_token);
    
    if (strlen(required_fw) > 0) {
        nvs_manager_set_string(DLM_OTA_NVS_NAMESPACE, DLM_OTA_NVS_KEY_CURRENT_VER, required_fw);
    }
    
    send_json_success(client_fd, NULL);
    
    vTaskDelay(pdMS_TO_TICKS(500));
    boot_manager_node_config_complete();
    
    return ESP_OK;
}

/* ============== /reset-wifi Handler ============== */

static esp_err_t reset_wifi_handler(int client_fd, const http_request_t *req)
{
    (void)req;
    
    wifi_manager_erase_credentials();
    send_json_success(client_fd, NULL);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;
}

/* ============== /reset-node Handler ============== */

static esp_err_t reset_node_handler(int client_fd, const http_request_t *req)
{
    (void)req;
    
    config_store_erase("node_id");
    config_store_erase("node_secret");
    config_store_erase("claim_token");
    
    send_json_success(client_fd, NULL);
    return ESP_OK;
}

/* ============== /continue Handler ============== */

static esp_err_t continue_handler(int client_fd, const http_request_t *req)
{
    (void)req;
    send_json_success(client_fd, NULL);
    vTaskDelay(pdMS_TO_TICKS(500));
    boot_manager_node_config_complete();
    return ESP_OK;
}

/* ============== Root/Settings Page Handler ============== */

static esp_err_t root_handler(int client_fd, const http_request_t *req)
{
    (void)req;
    http_server_send_response(client_fd, 200, HTTP_CONTENT_HTML,
                               index_html_start,
                               (size_t)(index_html_end - index_html_start));
    return ESP_OK;
}

/* ============== Router/Dispatcher ==============
 * Since we need client_fd in handlers, we register a single
 * dispatcher that routes based on path/method
 */

static esp_err_t portal_dispatcher(const http_request_t *req,
                                    http_response_t *resp,
                                    void *user_ctx)
{
    (void)resp;
    (void)user_ctx;
    int client_fd = req->client_fd;
    
    /* Route based on path and method */
    if (strcmp(req->path, "/") == 0 || strcmp(req->path, "/settings") == 0) {
        if (strcmp(req->method, "GET") == 0) {
            return root_handler(client_fd, req);
        } else if (strcmp(req->method, "POST") == 0) {
            return settings_handler(client_fd, req);
        }
    }
    
    if (strcmp(req->path, "/config") == 0 && strcmp(req->method, "GET") == 0) {
        return config_handler(client_fd, req);
    }
    
    if (strcmp(req->path, "/scan") == 0 && strcmp(req->method, "GET") == 0) {
        return scan_handler(client_fd, req);
    }
    
    if (strcmp(req->path, "/status") == 0 && strcmp(req->method, "GET") == 0) {
        return status_handler(client_fd, req);
    }
    
    if (strcmp(req->path, "/claim") == 0 && strcmp(req->method, "POST") == 0) {
        return claim_handler(client_fd, req);
    }
    
    if (strcmp(req->path, "/reset-wifi") == 0 && strcmp(req->method, "POST") == 0) {
        return reset_wifi_handler(client_fd, req);
    }
    
    if (strcmp(req->path, "/reset-node") == 0 && strcmp(req->method, "POST") == 0) {
        return reset_node_handler(client_fd, req);
    }
    
    if (strcmp(req->path, "/continue") == 0 && strcmp(req->method, "POST") == 0) {
        return continue_handler(client_fd, req);
    }
    
    /* 404 Not Found */
    http_server_send_error(client_fd, 404, "Not Found");
    return ESP_FAIL;
}

/* ============== Public API ============== */

esp_err_t web_portal_init(void)
{
    /* Register our dispatcher for all routes */
    /* We'll handle routing internally based on path */
    
    /* Catch-all handler for root paths */
    esp_err_t err = http_server_register_handler("GET", "/", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("POST", "/", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("GET", "/settings", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("POST", "/settings", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("GET", "/config", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("GET", "/scan", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("GET", "/status", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("POST", "/claim", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("POST", "/reset-wifi", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("POST", "/reset-node", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    err = http_server_register_handler("POST", "/continue", portal_dispatcher, NULL);
    if (err != ESP_OK) return err;
    
    /* Start DNS server for captive portal */
    char ap_ip[16];
    if (wifi_manager_get_ap_ip(ap_ip, sizeof(ap_ip)) == ESP_OK) {
        dns_server_start(ap_ip);
    }
    
    ESP_LOGI(TAG, "Web portal initialized");
    return ESP_OK;
}

void web_portal_deinit(void)
{
    dns_server_stop();
}
