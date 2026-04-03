/**
 * @file nvs_manager.c
 * @brief NVS (Non-Volatile Storage) abstraction layer
 * 
 * Provides a simplified interface to ESP-IDF's NVS API with
 * error handling, namespace management, and type safety.
 */

#include "nvs_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_manager";
static bool s_initialized = false;

esp_err_t nvs_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "NVS initialized successfully");
    return ESP_OK;
}

esp_err_t nvs_manager_erase_all(void)
{
    ESP_LOGW(TAG, "Erasing entire NVS partition!");
    return nvs_flash_erase();
}

/* ============== String Operations ============== */

esp_err_t nvs_manager_set_string(const char *namespace, const char *key, const char *value)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (namespace == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open namespace %s: %s", namespace, esp_err_to_name(err));
        return err;
    }
    
    err = nvs_set_str(handle, key, value);
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to set string %s: %s", key, esp_err_to_name(err));
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t nvs_manager_get_string(const char *namespace, const char *key, char *value, size_t max_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (namespace == NULL || key == NULL || value == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;  /* Namespace doesn't exist */
    }
    
    size_t required_size = 0;
    err = nvs_get_str(handle, key, NULL, &required_size);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    if (required_size > max_len) {
        nvs_close(handle);
        ESP_LOGW(TAG, "Value too long for key %s: need %d, have %d",
                 key, (int)required_size, (int)max_len);
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    
    err = nvs_get_str(handle, key, value, &required_size);
    nvs_close(handle);
    
    return err;
}

/* ============== Integer Operations ============== */

esp_err_t nvs_manager_set_int8(const char *namespace, const char *key, int8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_i8(handle, key, value);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_manager_get_int8(const char *namespace, const char *key, int8_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_get_i8(handle, key, value);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_manager_set_uint8(const char *namespace, const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_u8(handle, key, value);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_manager_get_uint8(const char *namespace, const char *key, uint8_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_get_u8(handle, key, value);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_manager_set_int32(const char *namespace, const char *key, int32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_i32(handle, key, value);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_manager_get_int32(const char *namespace, const char *key, int32_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_get_i32(handle, key, value);
    nvs_close(handle);
    return err;
}

/* ============== Binary/Blob Operations ============== */

esp_err_t nvs_manager_set_blob(const char *namespace, const char *key, 
                               const void *value, size_t length)
{
    if (length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_set_blob(handle, key, value, length);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_manager_get_blob(const char *namespace, const char *key,
                               void *value, size_t *length)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_get_blob(handle, key, value, length);
    nvs_close(handle);
    return err;
}

/* ============== Namespace Management ============== */

esp_err_t nvs_manager_erase_key(const char *namespace, const char *key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    
    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Key doesn't exist - not an error */
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_manager_erase_namespace(const char *namespace)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Namespace doesn't exist - not an error */
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    
    err = nvs_erase_all(handle);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Erased namespace: %s", namespace);
    return err;
}
