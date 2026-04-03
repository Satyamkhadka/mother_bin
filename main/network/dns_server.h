/**
 * @file dns_server.h
 * @brief DNS server for captive portal
 * 
 * Responds to all DNS queries with the AP's IP address,
 * forcing clients to connect to the captive portal.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start DNS server
 * 
 * Creates a task that listens on UDP port 53 and responds
 * to all queries with the configured IP address.
 * 
 * @param ip_addr   IP address to return for all queries (e.g., "192.168.4.1")
 * @return ESP_OK on success
 */
esp_err_t dns_server_start(const char *ip_addr);

/**
 * @brief Stop DNS server
 */
void dns_server_stop(void);

/**
 * @brief Check if DNS server is running
 * 
 * @return true if running
 */
bool dns_server_is_running(void);

#ifdef __cplusplus
}
#endif
