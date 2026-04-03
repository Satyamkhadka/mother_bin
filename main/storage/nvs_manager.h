/**
 * @file nvs_manager.h
 * @brief NVS (Non-Volatile Storage) abstraction layer
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize NVS flash storage
 * 
 * Erases and reinitializes if partition is corrupted.
 * Must be called before any other NVS operations.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t nvs_manager_init(void);

/**
 * @brief Erase entire NVS partition
 * 
 * WARNING: This erases ALL stored configuration!
 * 
 * @return ESP_OK on success
 */
esp_err_t nvs_manager_erase_all(void);

/* ============== String Operations ============== */

esp_err_t nvs_manager_set_string(const char *namespace, const char *key, const char *value);
esp_err_t nvs_manager_get_string(const char *namespace, const char *key, char *value, size_t max_len);

/* ============== Integer Operations ============== */

esp_err_t nvs_manager_set_int8(const char *namespace, const char *key, int8_t value);
esp_err_t nvs_manager_get_int8(const char *namespace, const char *key, int8_t *value);

esp_err_t nvs_manager_set_uint8(const char *namespace, const char *key, uint8_t value);
esp_err_t nvs_manager_get_uint8(const char *namespace, const char *key, uint8_t *value);

esp_err_t nvs_manager_set_int32(const char *namespace, const char *key, int32_t value);
esp_err_t nvs_manager_get_int32(const char *namespace, const char *key, int32_t *value);

/* ============== Binary/Blob Operations ============== */

esp_err_t nvs_manager_set_blob(const char *namespace, const char *key, 
                               const void *value, size_t length);
esp_err_t nvs_manager_get_blob(const char *namespace, const char *key,
                               void *value, size_t *length);

/* ============== Namespace Management ============== */

esp_err_t nvs_manager_erase_key(const char *namespace, const char *key);
esp_err_t nvs_manager_erase_namespace(const char *namespace);

#ifdef __cplusplus
}
#endif
