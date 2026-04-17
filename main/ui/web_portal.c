/**
 * @file web_portal.c
 * @brief Web portal HTTP handlers implementation
 */

#include "web_portal.h"
#include "web_assets/portal_page.h"
#include "form_parser.h"
#include "dlm_config.h"
#include "storage/config_store.h"
#include "network/wifi_manager.h"
#include "network/http_server.h"
#include "network/dns_server.h"
#include "core/boot_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
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
    /* Parse form data from body */
    form_field_t fields[FORM_MAX_FIELDS];
    int field_count = form_parser_parse(req->body, req->body_len, 
                                         fields, FORM_MAX_FIELDS);
    
    if (field_count < 0) {
        http_server_send_response(client_fd, 400, HTTP_CONTENT_HTML,
                                   error_html_start,
                                   (size_t)(error_html_end - error_html_start));
        return ESP_FAIL;
    }
    
    /* Extract WiFi credentials */
    const char *ssid = form_parser_get_value(fields, field_count, "hidden_ssid");
    if (ssid == NULL || strlen(ssid) == 0) {
        ssid = form_parser_get_value(fields, field_count, "selected_ssid");
    }
    const char *password = form_parser_get_value(fields, field_count, "password");
    if (password == NULL) password = "";
    
    if (ssid == NULL || strlen(ssid) == 0) {
        http_server_send_response(client_fd, 400, HTTP_CONTENT_HTML,
                                   error_html_start,
                                   (size_t)(error_html_end - error_html_start));
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Configuration received for SSID: %s", ssid);
    
    /* Save custom configuration fields */
    for (int i = 0; i < field_count; i++) {
        /* Skip WiFi fields */
        if (strcmp(fields[i].key, "ssid") == 0 ||
            strcmp(fields[i].key, "password") == 0 ||
            strcmp(fields[i].key, "hidden_ssid") == 0 ||
            strcmp(fields[i].key, "hiddenNetwork") == 0) {
            continue;
        }
        
        /* Try to save as each type - config_store will validate */
        /* String first */
        if (config_store_set_string(fields[i].key, fields[i].value) == ESP_OK) {
            continue;
        }
        
        /* Then boolean */
        if (strcmp(fields[i].value, "true") == 0) {
            config_store_set_bool(fields[i].key, true);
        } else if (strcmp(fields[i].value, "false") == 0) {
            config_store_set_bool(fields[i].key, false);
        }
        
        /* Then number */
        char *endptr;
        long num = strtol(fields[i].value, &endptr, 10);
        if (*endptr == '\0') {
            config_store_set_int(fields[i].key, (int32_t)num);
        }
    }
    
    /* Send success page */
    http_server_send_response(client_fd, 200, HTTP_CONTENT_HTML,
                               success_html_start,
                               (size_t)(success_html_end - success_html_start));
    
    /* Notify boot manager that provisioning is complete */
    /* Small delay to let response send */
    vTaskDelay(pdMS_TO_TICKS(500));
    
    boot_manager_provisioning_complete(ssid, password);
    
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
