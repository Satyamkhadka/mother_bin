/**
 * @file config_store.h
 * @brief Dynamic configuration storage API
 */

#pragma once

#include "dlm_types.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize configuration store
 * 
 * Must be called after nvs_manager_init()
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t config_store_init(void);

/**
 * @brief Set string configuration value
 * 
 * @param key   Configuration key name
 * @param value String value to store
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if type mismatch
 */
esp_err_t config_store_set_string(const char *key, const char *value);

/**
 * @brief Get string configuration value
 * 
 * Returns default value if key not found
 * 
 * @param key       Configuration key name
 * @param value     Output buffer
 * @param max_len   Buffer size
 * @return ESP_OK on success (even if returning default)
 */
esp_err_t config_store_get_string(const char *key, char *value, size_t max_len);

/**
 * @brief Set integer configuration value
 * 
 * @param key   Configuration key name
 * @param value Integer value to store
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of range or type mismatch
 */
esp_err_t config_store_set_int(const char *key, int32_t value);

/**
 * @brief Get integer configuration value
 * 
 * @param key       Configuration key name
 * @param value     Output pointer
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no default
 */
esp_err_t config_store_get_int(const char *key, int32_t *value);

/**
 * @brief Set boolean configuration value
 * 
 * @param key   Configuration key name
 * @param value Boolean value
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if type mismatch
 */
esp_err_t config_store_set_bool(const char *key, bool value);

/**
 * @brief Get boolean configuration value
 * 
 * @param key       Configuration key name
 * @param value     Output pointer
 * @return ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no default
 */
esp_err_t config_store_get_bool(const char *key, bool *value);

/**
 * @brief Check if a configuration key exists in storage
 * 
 * @param key   Configuration key name
 * @return true if key exists, false otherwise
 */
bool config_store_has_key(const char *key);

/**
 * @brief Erase a configuration key
 * 
 * @param key   Configuration key to erase
 * @return ESP_OK on success
 */
esp_err_t config_store_erase(const char *key);

/**
 * @brief Erase all configuration (factory reset)
 * 
 * @return ESP_OK on success
 */
esp_err_t config_store_erase_all(void);

/**
 * @brief Get configuration schema as JSON
 * 
 * Returns current values and metadata for all fields.
 * Used by /config HTTP endpoint.
 * 
 * @param buf       Output buffer for JSON
 * @param max_len   Buffer size
 * @return Number of bytes written (excluding null terminator), 0 on error
 */
size_t config_store_get_schema_json(char *buf, size_t max_len);

#ifdef __cplusplus
}
#endif
