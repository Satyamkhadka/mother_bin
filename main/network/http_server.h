/**
 * @file http_server.h
 * @brief Raw socket HTTP server
 * 
 * Lightweight HTTP server using raw sockets (no external dependencies).
 * Supports route registration with callbacks.
 */

#pragma once

#include "dlm_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP content types
 */
#define HTTP_CONTENT_HTML       "text/html"
#define HTTP_CONTENT_JSON       "application/json"
#define HTTP_CONTENT_CSS        "text/css"
#define HTTP_CONTENT_JS         "application/javascript"
#define HTTP_CONTENT_PLAIN      "text/plain"

/**
 * @brief Initialize HTTP server
 * 
 * Creates server socket and listening task.
 * 
 * @param port      Port to listen on (typically 80)
 * @return ESP_OK on success
 */
esp_err_t http_server_init(uint16_t port);

/**
 * @brief Deinitialize HTTP server
 */
void http_server_deinit(void);

/**
 * @brief Register HTTP route handler
 * 
 * @param method    HTTP method ("GET", "POST", etc.)
 * @param path      URL path (e.g., "/settings")
 * @param handler   Callback function
 * @param user_ctx  User context passed to handler
 * @return ESP_OK on success
 */
esp_err_t http_server_register_handler(const char *method, const char *path,
                                        http_handler_fn_t handler, void *user_ctx);

/**
 * @brief Send HTTP response (helper for handlers)
 * 
 * @param client_fd     Client socket
 * @param status_code   HTTP status (200, 404, etc.)
 * @param content_type  Content-Type header
 * @param body          Response body
 * @param body_len      Body length (or 0 for strlen)
 * @return ESP_OK on success
 */
esp_err_t http_server_send_response(int client_fd, int status_code,
                                     const char *content_type,
                                     const char *body, size_t body_len);

/**
 * @brief Send redirect response
 * 
 * @param client_fd     Client socket
 * @param location      Redirect URL
 * @return ESP_OK on success
 */
esp_err_t http_server_send_redirect(int client_fd, const char *location);

/**
 * @brief Send CORS preflight response (OPTIONS)
 * 
 * @param client_fd     Client socket
 * @return ESP_OK on success
 */
esp_err_t http_server_send_cors_preflight(int client_fd);

/**
 * @brief Send error response
 * 
 * @param client_fd     Client socket
 * @param status_code   Error code (404, 500, etc.)
 * @param message       Error message
 * @return ESP_OK on success
 */
esp_err_t http_server_send_error(int client_fd, int status_code, 
                                  const char *message);

/**
 * @brief URL decode a string (in-place or to new buffer)
 * 
 * @param src       Source string
 * @param dst       Destination buffer (can be same as src)
 * @param dst_len   Destination buffer size
 * @return Number of bytes written
 */
size_t http_server_url_decode(const char *src, char *dst, size_t dst_len);

/**
 * @brief Parse query string parameters
 * 
 * @param query     Query string (after ?)
 * @param params    Output parameter array
 * @param max_params    Max number of params to parse
 * @return Number of params parsed
 */
typedef struct {
    char *key;
    char *val;
} http_param_t;

int http_server_parse_query(const char *query, 
                             http_param_t *params,
                             int max_params);

#ifdef __cplusplus
}
#endif
