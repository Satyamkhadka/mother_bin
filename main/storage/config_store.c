/**
 * @file config_store.c
 * @brief Dynamic configuration storage using NVS
 * 
 * This module stores and retrieves device configuration fields
 * defined in the configuration schema. All values are persisted
 * in NVS and survive power cycles.
 */

#include "config_store.h"
#include "dlm_config.h"
#include "dlm_types.h"
#include "nvs_manager.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "config_store";

/* ============== Default Configuration Schema ==============
 * These fields appear in the web UI automatically.
 * Add your custom fields here.
 */
const config_field_def_t dlm_config_schema[] = {
    /* Device Identity */
    {
        .name = "device_name",
        .label = "Device Name",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "ELM-Device",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    {
        .name = "hardware_version",
        .label = "Hardware Version",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "v1",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },

    /* Backend Connectivity */
    {
        .name = "backend_url",
        .label = "Backend Server URL",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    
    /* Node Identity */
    {
        .name = "node_id",
        .label = "Node ID",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    {
        .name = "node_secret",
        .label = "Node Secret",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    {
        .name = "claim_token",
        .label = "Claim Token",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    
    /* Device Classification */
    {
        .name = "device",
        .label = "Device",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "not-alloted",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    {
        .name = "device_type",
        .label = "Device Type",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "not-alloted",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    {
        .name = "sub_type",
        .label = "Sub Type",
        .type = CONFIG_FIELD_TYPE_STRING,
        .default_str = "",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },

    /* Update Configuration */
    {
        .name = "update_interval_min",
        .label = "Update Check Interval (minutes)",
        .type = CONFIG_FIELD_TYPE_NUMBER,
        .default_str = NULL,
        .default_num = 60,
        .min_value = 5,
        .max_value = 1440  // 24 hours
    },
    
    /* Feature Toggles */
    {
        .name = "auto_update",
        .label = "Automatic Updates",
        .type = CONFIG_FIELD_TYPE_BOOLEAN,
        .default_str = "false",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    {
        .name = "debug_mode",
        .label = "Enable Debug Mode",
        .type = CONFIG_FIELD_TYPE_BOOLEAN,
        .default_str = "false",
        .default_num = 0,
        .min_value = 0,
        .max_value = 0
    },
    
    /* NOTE: Add your custom configuration fields below.
     * Example:
     * {
     *     .name = "mqtt_broker",
     *     .label = "MQTT Broker URL",
     *     .type = CONFIG_FIELD_TYPE_STRING,
     *     .default_str = "mqtt://broker.example.com",
     *     .default_num = 0,
     *     .min_value = 0,
     *     .max_value = 0
     * },
     * {
     *     .name = "sensor_calibration",
     *     .label = "Sensor Calibration",
     *     .type = CONFIG_FIELD_TYPE_NUMBER,
     *     .default_str = NULL,
     *     .default_num = 100,
     *     .min_value = 0,
     *     .max_value = 1000
     * },
     */
};

const size_t dlm_config_schema_count = ARRAY_SIZE(dlm_config_schema);

/* ============== Internal Helpers ============== */

static const config_field_def_t* find_field_def(const char *name)
{
    for (size_t i = 0; i < dlm_config_schema_count; i++) {
        if (strcmp(dlm_config_schema[i].name, name) == 0) {
            return &dlm_config_schema[i];
        }
    }
    return NULL;
}

/* ============== Public API ============== */

esp_err_t config_store_init(void)
{
    /* NVS is already initialized by nvs_manager */
    ESP_LOGI(TAG, "Config store initialized with %d fields", 
             (int)dlm_config_schema_count);
    return ESP_OK;
}

esp_err_t config_store_set_string(const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const config_field_def_t *field = find_field_def(key);
    if (field == NULL) {
        ESP_LOGW(TAG, "Setting unknown config field: %s", key);
        /* Still allow it - user might have custom fields */
    } else if (field->type != CONFIG_FIELD_TYPE_STRING) {
        ESP_LOGE(TAG, "Type mismatch: %s is not a string field", key);
        return ESP_ERR_INVALID_ARG;
    }
    
    return nvs_manager_set_string(DLM_CONFIG_NVS_NAMESPACE, key, value);
}

esp_err_t config_store_get_string(const char *key, char *value, size_t max_len)
{
    if (key == NULL || value == NULL || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = nvs_manager_get_string(DLM_CONFIG_NVS_NAMESPACE, key, value, max_len);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Return default value */
        const config_field_def_t *field = find_field_def(key);
        if (field != NULL && field->type == CONFIG_FIELD_TYPE_STRING) {
            if (field->default_str != NULL) {
                strlcpy(value, field->default_str, max_len);
                return ESP_OK;
            }
        }
        /* No default, return empty string */
        value[0] = '\0';
        return ESP_OK;
    }
    
    return err;
}

esp_err_t config_store_set_int(const char *key, int32_t value)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const config_field_def_t *field = find_field_def(key);
    if (field != NULL) {
        if (field->type != CONFIG_FIELD_TYPE_NUMBER) {
            ESP_LOGE(TAG, "Type mismatch: %s is not a number field", key);
            return ESP_ERR_INVALID_ARG;
        }
        /* Validate range */
        if (value < field->min_value || value > field->max_value) {
            ESP_LOGE(TAG, "Value %d out of range [%d, %d] for %s",
                     value, field->min_value, field->max_value, key);
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    return nvs_manager_set_int32(DLM_CONFIG_NVS_NAMESPACE, key, value);
}

esp_err_t config_store_get_int(const char *key, int32_t *value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = nvs_manager_get_int32(DLM_CONFIG_NVS_NAMESPACE, key, value);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Return default value */
        const config_field_def_t *field = find_field_def(key);
        if (field != NULL && field->type == CONFIG_FIELD_TYPE_NUMBER) {
            *value = field->default_num;
            return ESP_OK;
        }
        return ESP_ERR_NVS_NOT_FOUND;
    }
    
    return err;
}

esp_err_t config_store_set_bool(const char *key, bool value)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const config_field_def_t *field = find_field_def(key);
    if (field != NULL && field->type != CONFIG_FIELD_TYPE_BOOLEAN) {
        ESP_LOGE(TAG, "Type mismatch: %s is not a boolean field", key);
        return ESP_ERR_INVALID_ARG;
    }
    
    return nvs_manager_set_uint8(DLM_CONFIG_NVS_NAMESPACE, key, value ? 1 : 0);
}

esp_err_t config_store_get_bool(const char *key, bool *value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t u8_val;
    esp_err_t err = nvs_manager_get_uint8(DLM_CONFIG_NVS_NAMESPACE, key, &u8_val);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Return default value */
        const config_field_def_t *field = find_field_def(key);
        if (field != NULL && field->type == CONFIG_FIELD_TYPE_BOOLEAN) {
            *value = (field->default_str != NULL && 
                     strcmp(field->default_str, "true") == 0);
            return ESP_OK;
        }
        return ESP_ERR_NVS_NOT_FOUND;
    } else if (err != ESP_OK) {
        return err;
    }
    
    *value = (u8_val != 0);
    return ESP_OK;
}

bool config_store_has_key(const char *key)
{
    char buf[4];
    esp_err_t err = nvs_manager_get_string(DLM_CONFIG_NVS_NAMESPACE, key, buf, sizeof(buf));
    return (err == ESP_OK);
}

esp_err_t config_store_erase(const char *key)
{
    return nvs_manager_erase_key(DLM_CONFIG_NVS_NAMESPACE, key);
}

esp_err_t config_store_erase_all(void)
{
    return nvs_manager_erase_namespace(DLM_CONFIG_NVS_NAMESPACE);
}

size_t config_store_get_schema_json(char *buf, size_t max_len)
{
    if (buf == NULL || max_len == 0) {
        return 0;
    }
    
    size_t pos = 0;
    int written;
    
    written = snprintf(buf + pos, max_len - pos, "{\"fields\":[");
    if (written < 0 || (size_t)written >= max_len - pos) return 0;
    pos += written;
    
    for (size_t i = 0; i < dlm_config_schema_count; i++) {
        const config_field_def_t *f = &dlm_config_schema[i];
        
        /* Get current value or default */
        char current_str[128] = "";
        int32_t current_num = 0;
        bool current_bool = false;
        
        esp_err_t err = ESP_ERR_NVS_NOT_FOUND;
        
        switch (f->type) {
            case CONFIG_FIELD_TYPE_STRING:
                err = config_store_get_string(f->name, current_str, sizeof(current_str));
                written = snprintf(buf + pos, max_len - pos,
                    "{\"name\":\"%s\",\"label\":\"%s\",\"type\":\"string\",\"value\":\"%s\"}",
                    f->name, f->label, 
                    (err == ESP_OK) ? current_str : (f->default_str ? f->default_str : ""));
                break;
                
            case CONFIG_FIELD_TYPE_NUMBER:
                err = config_store_get_int(f->name, &current_num);
                written = snprintf(buf + pos, max_len - pos,
                    "{\"name\":\"%s\",\"label\":\"%s\",\"type\":\"number\",\"value\":%d,\"min\":%d,\"max\":%d}",
                    f->name, f->label,
                    (err == ESP_OK) ? (int)current_num : f->default_num,
                    f->min_value, f->max_value);
                break;
                
            case CONFIG_FIELD_TYPE_BOOLEAN:
                err = config_store_get_bool(f->name, &current_bool);
                if (err != ESP_OK) {
                    current_bool = (f->default_str != NULL && strcmp(f->default_str, "true") == 0);
                }
                written = snprintf(buf + pos, max_len - pos,
                    "{\"name\":\"%s\",\"label\":\"%s\",\"type\":\"boolean\",\"value\":%s}",
                    f->name, f->label, current_bool ? "true" : "false");
                break;
        }
        
        if (written < 0 || (size_t)written >= max_len - pos) return 0;
        pos += written;
        
        /* Add comma if not last */
        if (i < dlm_config_schema_count - 1) {
            written = snprintf(buf + pos, max_len - pos, ",");
            if (written < 0 || (size_t)written >= max_len - pos) return 0;
            pos += written;
        }
    }
    
    written = snprintf(buf + pos, max_len - pos, "]}");
    if (written < 0 || (size_t)written >= max_len - pos) return 0;
    pos += written;
    
    return pos;
}
