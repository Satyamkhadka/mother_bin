/**
 * @file http_server.c
 * @brief Raw socket HTTP server implementation
 */

#include "http_server.h"
#include "dlm_config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "http_server";

#define HTTP_MAX_REQUEST_SIZE   2048
#define HTTP_MAX_HEADERS        16
#define HTTP_MAX_ROUTES         16
#define HTTP_CLIENT_TIMEOUT_MS  5000

/* ============== Route Table ============== */

typedef struct {
    char method[8];
    char path[64];
    http_handler_fn_t handler;
    void *user_ctx;
    bool active;
} http_route_t;

static struct {
    bool initialized;
    int server_fd;
    uint16_t port;
    TaskHandle_t task_handle;
    http_route_t routes[HTTP_MAX_ROUTES];
} s_server = {0};

/* ============== HTTP Status Strings ============== */

static const char* http_status_str(int code)
{
    switch (code) {
        case 200: return "OK";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

/* ============== URL Utilities ============== */

size_t http_server_url_decode(const char *src, char *dst, size_t dst_len)
{
    size_t i = 0, j = 0;
    
    while (src[i] && j < dst_len - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            /* Hex decode */
            char hex[3] = {src[i+1], src[i+2], '\0'};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return j;
}

/* ============== Response Helpers ============== */

esp_err_t http_server_send_response(int client_fd, int status_code,
                                     const char *content_type,
                                     const char *body, size_t body_len)
{
    if (body_len == 0 && body != NULL) {
        body_len = strlen(body);
    }
    
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "Expires: 0\r\n"
        "\r\n",
        status_code, http_status_str(status_code),
        content_type,
        (int)body_len
    );
    
    /* Send header */
    ssize_t sent = send(client_fd, header, header_len, 0);
    if (sent < 0) {
        return ESP_FAIL;
    }
    
    /* Send body */
    if (body != NULL && body_len > 0) {
        sent = send(client_fd, body, body_len, 0);
        if (sent < 0) {
            return ESP_FAIL;
        }
    }
    
    return ESP_OK;
}

esp_err_t http_server_send_redirect(int client_fd, const char *location)
{
    char response[512];
    snprintf(response, sizeof(response),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        location
    );
    
    ssize_t sent = send(client_fd, response, strlen(response), 0);
    return (sent > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t http_server_send_cors_preflight(int client_fd)
{
    const char *response = 
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Connection: close\r\n"
        "\r\n";
    
    ssize_t sent = send(client_fd, response, strlen(response), 0);
    return (sent > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t http_server_send_error(int client_fd, int status_code, 
                                  const char *message)
{
    char body[256];
    snprintf(body, sizeof(body),
        "<!DOCTYPE html>"
        "<html><head><title>Error %d</title></head>"
        "<body><h1>Error %d</h1><p>%s</p></body></html>",
        status_code, status_code, message
    );
    
    return http_server_send_response(client_fd, status_code, 
                                      HTTP_CONTENT_HTML, body, 0);
}

/* ============== Request Parsing ============== */

static int parse_request_line(const char *line, http_request_t *req)
{
    char method[8] = {0};
    char path[256] = {0};
    char version[16] = {0};
    
    if (sscanf(line, "%7s %255s %15s", method, path, version) != 3) {
        return -1;
    }
    
    req->method = strdup(method);  /* Will be freed after handling */
    
    /* Parse path and query string */
    char *q = strchr(path, '?');
    if (q) {
        *q = '\0';
        req->path = strdup(path);
        req->query_string = strdup(q + 1);
    } else {
        req->path = strdup(path);
        req->query_string = NULL;
    }
    
    return 0;
}

static int parse_headers(const char *data, size_t len, 
                         http_request_t *req, size_t *header_end)
{
    /* Find end of headers (blank line) - try CRLF first, then LF only */
    const char *end = strstr(data, "\r\n\r\n");
    if (end) {
        *header_end = (end - data) + 4;
    } else {
        end = strstr(data, "\n\n");
        if (end) {
            *header_end = (end - data) + 2;
        } else {
            return -1;  /* Incomplete headers */
        }
    }
    
    /* Parse Content-Length for body */
    const char *cl = strcasestr(data, "Content-Length:");
    if (cl) {
        req->body_len = atoi(cl + 15);
    } else {
        req->body_len = 0;
    }
    
    return 0;
}

/* ============== Route Matching ============== */

static http_route_t* find_route(const char *method, const char *path)
{
    for (int i = 0; i < HTTP_MAX_ROUTES; i++) {
        if (!s_server.routes[i].active) continue;
        
        if (strcasecmp(s_server.routes[i].method, method) == 0 &&
            strcmp(s_server.routes[i].path, path) == 0) {
            return &s_server.routes[i];
        }
    }
    return NULL;
}

/* ============== Client Handler ============== */

static void handle_client(int client_fd)
{
    char buffer[HTTP_MAX_REQUEST_SIZE];
    int received = 0;
    int total_received = 0;
    
    /* Set receive timeout */
    struct timeval tv;
    tv.tv_sec = HTTP_CLIENT_TIMEOUT_MS / 1000;
    tv.tv_usec = (HTTP_CLIENT_TIMEOUT_MS % 1000) * 1000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    /* Read request */
    while (total_received < HTTP_MAX_REQUEST_SIZE - 1) {
        received = recv(client_fd, buffer + total_received, 
                       HTTP_MAX_REQUEST_SIZE - 1 - total_received, 0);
        if (received <= 0) {
            break;
        }
        total_received += received;
        buffer[total_received] = '\0';
        
        /* Check if we have complete request */
        if (strstr(buffer, "\r\n\r\n") || strstr(buffer, "\n\n")) {
            break;
        }
    }
    
    if (total_received == 0) {
        close(client_fd);
        return;
    }
    
    /* Ensure null termination */
    buffer[total_received] = '\0';
    
    ESP_LOGD(TAG, "Received %d bytes: %.100s...", total_received, buffer);
    
    /* Check if we have complete headers */
    if (!strstr(buffer, "\r\n\r\n") && !strstr(buffer, "\n\n")) {
        ESP_LOGW(TAG, "Incomplete HTTP headers received");
        http_server_send_error(client_fd, 400, "Incomplete Request");
        close(client_fd);
        return;
    }
    
    /* Parse request */
    http_request_t req = {0};
    http_response_t resp = {0};
    req.client_fd = client_fd;
    
    /* Parse headers first (before we modify buffer for request line) */
    size_t header_end;
    if (parse_headers(buffer, total_received, &req, &header_end) != 0) {
        ESP_LOGW(TAG, "Failed to parse headers from: %.100s", buffer);
        http_server_send_error(client_fd, 400, "Invalid Headers");
        close(client_fd);
        return;
    }
    
    /* Get first line */
    char *line_end = strstr(buffer, "\r\n");
    if (!line_end) {
        line_end = strchr(buffer, '\n');
    }
    
    if (!line_end) {
        http_server_send_error(client_fd, 400, "Bad Request");
        close(client_fd);
        return;
    }
    
    *line_end = '\0';
    
    if (parse_request_line(buffer, &req) != 0) {
        http_server_send_error(client_fd, 400, "Invalid Request Line");
        close(client_fd);
        return;
    }
    
    /* Get body if present */
    if (req.body_len > 0) {
        size_t body_received = total_received - header_end;
        if (body_received >= req.body_len) {
            req.body = buffer + header_end;
        } else {
            /* Need to read more body data */
            /* For simplicity, limit body size */
            if (req.body_len > HTTP_MAX_REQUEST_SIZE - header_end - 1) {
                req.body_len = HTTP_MAX_REQUEST_SIZE - header_end - 1;
            }
            /* Read remaining body */
            while (body_received < req.body_len) {
                received = recv(client_fd, buffer + header_end + body_received,
                              req.body_len - body_received, 0);
                if (received <= 0) break;
                body_received += received;
            }
            buffer[header_end + body_received] = '\0';
            req.body = buffer + header_end;
        }
    }
    
    ESP_LOGD(TAG, "%s %s", req.method, req.path);
    
    /* Handle CORS preflight (OPTIONS) requests */
    if (strcasecmp(req.method, "OPTIONS") == 0) {
        http_server_send_cors_preflight(client_fd);
        free((void*)req.method);
        free((void*)req.path);
        free((void*)req.query_string);
        close(client_fd);
        return;
    }
    
    /* Find and call handler */
    http_route_t *route = find_route(req.method, req.path);
    
    if (route) {
        esp_err_t err = route->handler(&req, &resp, route->user_ctx);
        if (err != ESP_OK) {
            http_server_send_error(client_fd, 500, "Internal Server Error");
        } else {
            /* Handler should have sent response */
        }
    } else {
        /* For captive portal: redirect all unmatched requests to portal */
        if (strcmp(req.path, "/") == 0) {
            http_server_send_redirect(client_fd, "/settings");
        } else {
            /* For captive portal detection URLs, redirect to main page */
            ESP_LOGD(TAG, "Captive portal redirect for: %s", req.path);
            http_server_send_redirect(client_fd, "http://192.168.4.1/settings");
        }
    }
    
    /* Cleanup */
    free((void*)req.method);
    free((void*)req.path);
    free((void*)req.query_string);
    
    close(client_fd);
}

/* ============== Server Task ============== */

static void http_server_task(void *pvParameters)
{
    (void)pvParameters;
    
    ESP_LOGI(TAG, "HTTP server listening on port %d", s_server.port);
    
    while (s_server.initialized) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(s_server.server_fd, 
                              (struct sockaddr *)&client_addr, 
                              &client_len);
        
        if (client_fd < 0) {
            if (s_server.initialized) {
                ESP_LOGE(TAG, "Accept failed: errno %d", errno);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }
        
        /* Handle client (could spawn task for concurrent handling) */
        handle_client(client_fd);
    }
    
    s_server.task_handle = NULL;
    vTaskDelete(NULL);
}

/* ============== Public API ============== */

esp_err_t http_server_init(uint16_t port)
{
    if (s_server.initialized) {
        return ESP_OK;
    }
    
    s_server.port = port;
    
    /* Create server socket */
    s_server.server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_server.server_fd < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        return ESP_FAIL;
    }
    
    /* Allow reuse */
    int opt = 1;
    setsockopt(s_server.server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    /* Bind */
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    
    if (bind(s_server.server_fd, (struct sockaddr *)&server_addr, 
             sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind: errno %d", errno);
        close(s_server.server_fd);
        s_server.server_fd = -1;
        return ESP_FAIL;
    }
    
    /* Listen */
    if (listen(s_server.server_fd, 5) < 0) {
        ESP_LOGE(TAG, "Failed to listen: errno %d", errno);
        close(s_server.server_fd);
        s_server.server_fd = -1;
        return ESP_FAIL;
    }
    
    /* Start task */
    s_server.initialized = true;
    BaseType_t ret = xTaskCreate(
        http_server_task,
        "http_server",
        DLM_HTTP_TASK_STACK_SIZE,
        NULL,
        DLM_HTTP_TASK_PRIORITY,
        &s_server.task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create HTTP task");
        close(s_server.server_fd);
        s_server.server_fd = -1;
        s_server.initialized = false;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "HTTP server initialized on port %d", port);
    return ESP_OK;
}

void http_server_deinit(void)
{
    if (!s_server.initialized) {
        return;
    }
    
    s_server.initialized = false;
    
    if (s_server.server_fd >= 0) {
        close(s_server.server_fd);
        s_server.server_fd = -1;
    }
    
    if (s_server.task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t http_server_register_handler(const char *method, const char *path,
                                        http_handler_fn_t handler, void *user_ctx)
{
    if (method == NULL || path == NULL || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Find free slot */
    for (int i = 0; i < HTTP_MAX_ROUTES; i++) {
        if (!s_server.routes[i].active) {
            strlcpy(s_server.routes[i].method, method, 
                    sizeof(s_server.routes[i].method));
            strlcpy(s_server.routes[i].path, path,
                    sizeof(s_server.routes[i].path));
            s_server.routes[i].handler = handler;
            s_server.routes[i].user_ctx = user_ctx;
            s_server.routes[i].active = true;
            
            ESP_LOGD(TAG, "Registered handler: %s %s", method, path);
            return ESP_OK;
        }
    }
    
    ESP_LOGE(TAG, "Route table full");
    return ESP_ERR_NO_MEM;
}
