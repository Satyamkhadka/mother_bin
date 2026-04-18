/**
 * @file custom_provider.h
 * @brief Firmware Manager OTA Provider
 */

#pragma once

#include "update/ota_provider.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

const ota_provider_t* ota_provider_get_custom(void);

/**
 * @brief Exchange a claim token for node credentials
 *
 * POSTs to the server's claim endpoint with device details.
 *
 * @param server_url            Backend base URL
 * @param claim_token           User-provided claim token
 * @param node_id               Output buffer for node ID
 * @param node_id_len           Size of node_id buffer
 * @param node_secret           Output buffer for node secret
 * @param node_secret_len       Size of node_secret buffer
 * @param required_fw_version   Output buffer for required firmware version (optional)
 * @param fw_ver_len            Size of fw_ver buffer
 * @return ESP_OK on successful claim
 */
esp_err_t custom_provider_claim_device(const char *server_url,
                                        const char *claim_token,
                                        char *node_id, size_t node_id_len,
                                        char *node_secret, size_t node_secret_len,
                                        char *required_fw_version, size_t fw_ver_len);

#ifdef __cplusplus
}
#endif
