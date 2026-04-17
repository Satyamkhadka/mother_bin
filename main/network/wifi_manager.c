/**
 * @file wifi_manager.c
 * @brief WiFi connection management implementation
 */

#include "wifi_manager.h"
#include "dlm_config.h"
#include "storage/nvs_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/inet.h"
#include <string.h>

static const char *TAG = "wifi_manager";

/* ============== State ============== */

typedef struct {
    bool initialized;
    dlm_wifi_state_t state;
    esp_netif_t *sta_netif;
    esp_netif_t *ap_netif;
    
    /* Scan results */
    wifi_scan_result_t scan_results[DLM_WIFI_MAX_SCAN_RESULTS];
    size_t scan_count;
    
    /* Connection tracking */
    bool has_ip;
    esp_ip4_addr_t ip_addr;
} wifi_state_t;

static wifi_state_t s_wifi = {0};

/* ============== Event Handler ============== */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started");
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA connected to AP");
                s_wifi.state = WIFI_STATE_STA_CONNECTED;
                /* Wait for IP_EVENT_STA_GOT_IP for full connection */
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *evt = event_data;
                ESP_LOGW(TAG, "STA disconnected, reason: %d", evt->reason);
                s_wifi.has_ip = false;
                s_wifi.state = WIFI_STATE_STA_FAILED;
                
                /* Notify boot manager */
                boot_manager_on_wifi_failed();
                break;
            }
            
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started");
                s_wifi.state = WIFI_STATE_AP_MODE;
                break;
                
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "AP stopped");
                if (s_wifi.state == WIFI_STATE_AP_MODE) {
                    s_wifi.state = WIFI_STATE_IDLE;
                }
                break;
                
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *evt = event_data;
                ESP_LOGI(TAG, "Station connected to AP: " MACSTR, MAC2STR(evt->mac));
                break;
            }
            
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *evt = event_data;
                ESP_LOGI(TAG, "Station disconnected from AP: " MACSTR, MAC2STR(evt->mac));
                break;
            }
            
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *evt = event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
                s_wifi.ip_addr = evt->ip_info.ip;
                s_wifi.has_ip = true;
                s_wifi.state = WIFI_STATE_STA_CONNECTED;
                
                /* Notify boot manager */
                boot_manager_on_wifi_connected();
                break;
            }
            
            case IP_EVENT_STA_LOST_IP:
                ESP_LOGW(TAG, "Lost IP");
                s_wifi.has_ip = false;
                break;
                
            default:
                break;
        }
    }
}

/* ============== Initialization ============== */

esp_err_t wifi_manager_init(void)
{
    if (s_wifi.initialized) {
        return ESP_OK;
    }

    /* Initialize TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    
    /* Create default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    /* Create netifs */
    s_wifi.sta_netif = esp_netif_create_default_wifi_sta();
    if (s_wifi.sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create STA netif");
        return ESP_FAIL;
    }
    
    s_wifi.ap_netif = esp_netif_create_default_wifi_ap();
    if (s_wifi.ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create AP netif");
        return ESP_FAIL;
    }
    
    /* Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    /* Register event handler */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    
    s_wifi.initialized = true;
    s_wifi.state = WIFI_STATE_IDLE;
    
    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

void wifi_manager_deinit(void)
{
    if (!s_wifi.initialized) {
        return;
    }
    
    esp_wifi_stop();
    esp_wifi_deinit();
    
    s_wifi.initialized = false;
    s_wifi.state = WIFI_STATE_IDLE;
}

/* ============== Credential Management ============== */

bool wifi_manager_has_credentials(void)
{
    char ssid[WIFI_MAX_SSID_LEN];
    esp_err_t err = nvs_manager_get_string(DLM_WIFI_NVS_NAMESPACE, 
                                           DLM_WIFI_NVS_KEY_SSID,
                                           ssid, sizeof(ssid));
    return (err == ESP_OK && strlen(ssid) > 0);
}

esp_err_t wifi_manager_get_credentials(char *ssid, size_t ssid_len,
                                        char *pass, size_t pass_len)
{
    esp_err_t err = nvs_manager_get_string(DLM_WIFI_NVS_NAMESPACE,
                                           DLM_WIFI_NVS_KEY_SSID,
                                           ssid, ssid_len);
    if (err != ESP_OK) {
        return err;
    }
    
    err = nvs_manager_get_string(DLM_WIFI_NVS_NAMESPACE,
                                 DLM_WIFI_NVS_KEY_PASS,
                                 pass, pass_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* No password stored - open network */
        pass[0] = '\0';
    } else if (err != ESP_OK) {
        return err;
    }
    
    return ESP_OK;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = nvs_manager_set_string(DLM_WIFI_NVS_NAMESPACE,
                                           DLM_WIFI_NVS_KEY_SSID,
                                           ssid);
    if (err != ESP_OK) {
        return err;
    }
    
    err = nvs_manager_set_string(DLM_WIFI_NVS_NAMESPACE,
                                 DLM_WIFI_NVS_KEY_PASS,
                                 password ? password : "");
    if (err != ESP_OK) {
        return err;
    }
    
    ESP_LOGI(TAG, "WiFi credentials saved for: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_erase_credentials(void)
{
    nvs_manager_erase_key(DLM_WIFI_NVS_NAMESPACE, DLM_WIFI_NVS_KEY_SSID);
    nvs_manager_erase_key(DLM_WIFI_NVS_NAMESPACE, DLM_WIFI_NVS_KEY_PASS);
    return ESP_OK;
}

/* ============== Connection Management ============== */

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (!s_wifi.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Keep AP running during connection attempt - stop only on success */
    
    /* Configure STA */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {0},
            .password = {0},
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (password && strlen(password) > 0) {
        strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    }
    
    /* Set mode to APSTA so AP stays active during connection */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    /* Start WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());
    
    s_wifi.state = WIFI_STATE_STA_CONNECTING;
    ESP_LOGI(TAG, "Connecting to %s...", ssid);
    
    return ESP_OK;
}

esp_err_t wifi_manager_disconnect(void)
{
    if (!s_wifi.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_wifi.has_ip = false;
    return esp_wifi_disconnect();
}

bool wifi_manager_is_connected(void)
{
    return (s_wifi.state == WIFI_STATE_STA_CONNECTED && s_wifi.has_ip);
}

dlm_wifi_state_t wifi_manager_get_state(void)
{
    return s_wifi.state;
}

esp_err_t wifi_manager_wait_for_connection(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        if (wifi_manager_is_connected()) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }
    return ESP_ERR_TIMEOUT;
}

/* ============== AP Mode ============== */

esp_err_t wifi_manager_start_ap(void)
{
    if (!s_wifi.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Get MAC address for unique SSID */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    
    char ssid[33];
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X%02X%02X%02X%02X",
             DLM_AP_SSID_PREFIX, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    /* Configure AP */
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = {0},
            .ssid_len = strlen(ssid),
            .channel = DLM_AP_CHANNEL,
            .max_connection = DLM_AP_MAX_CONNECTIONS,
            .authmode = DLM_AP_PASSWORD ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
        },
    };
    
    memcpy(wifi_config.ap.ssid, ssid, wifi_config.ap.ssid_len);
    
    if (DLM_AP_PASSWORD) {
        strlcpy((char*)wifi_config.ap.password, DLM_AP_PASSWORD, 
                sizeof(wifi_config.ap.password));
    }
    
    /* Set AP IP configuration */
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = ipaddr_addr(DLM_AP_IP_ADDR);
    ip_info.gw.addr = ipaddr_addr(DLM_AP_GATEWAY);
    ip_info.netmask.addr = ipaddr_addr(DLM_AP_NETMASK);
    
    esp_netif_dhcps_stop(s_wifi.ap_netif);
    esp_netif_set_ip_info(s_wifi.ap_netif, &ip_info);
    esp_netif_dhcps_start(s_wifi.ap_netif);
    
    /* Set mode to AP */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    /* Start WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());
    
    s_wifi.state = WIFI_STATE_AP_MODE;
    ESP_LOGI(TAG, "AP started: %s (IP: %s)", ssid, DLM_AP_IP_ADDR);
    
    return ESP_OK;
}

esp_err_t wifi_manager_stop_ap(void)
{
    if (s_wifi.state != WIFI_STATE_AP_MODE) {
        return ESP_OK;
    }
    
    ESP_ERROR_CHECK(esp_wifi_stop());
    s_wifi.state = WIFI_STATE_IDLE;
    
    ESP_LOGI(TAG, "AP stopped");
    return ESP_OK;
}

bool wifi_manager_is_ap_active(void)
{
    return (s_wifi.state == WIFI_STATE_AP_MODE);
}

int wifi_manager_get_ap_sta_count(void)
{
    if (!wifi_manager_is_ap_active()) {
        return 0;
    }
    
    wifi_sta_list_t sta_list;
    esp_wifi_ap_get_sta_list(&sta_list);
    return sta_list.num;
}

/* ============== WiFi Scan ============== */

esp_err_t wifi_manager_scan(bool hidden)
{
    if (!s_wifi.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Stop current connection if any */
    if (s_wifi.state == WIFI_STATE_STA_CONNECTING) {
        esp_wifi_disconnect();
    }
    
    /* Configure scan */
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = hidden,
    };
    
    /* Set mode to STA for scanning */
    bool was_idle = (s_wifi.state == WIFI_STATE_IDLE);
    if (was_idle) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    
    /* Start scan (blocking) */
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    
    /* Stop WiFi if we started it just for scanning */
    if (was_idle) {
        esp_wifi_stop();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
        s_wifi.state = WIFI_STATE_IDLE;
    }
    
    /* Get results */
    uint16_t num_records = DLM_WIFI_MAX_SCAN_RESULTS;
    wifi_ap_record_t records[DLM_WIFI_MAX_SCAN_RESULTS];
    
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&num_records, records));
    
    /* Copy to our format */
    s_wifi.scan_count = num_records;
    for (size_t i = 0; i < num_records && i < DLM_WIFI_MAX_SCAN_RESULTS; i++) {
        strlcpy(s_wifi.scan_results[i].ssid, (char*)records[i].ssid,
                sizeof(s_wifi.scan_results[i].ssid));
        s_wifi.scan_results[i].rssi = records[i].rssi;
        s_wifi.scan_results[i].channel = records[i].primary;
        s_wifi.scan_results[i].auth_mode = records[i].authmode;
        s_wifi.scan_results[i].is_hidden = false;  /* Not exposed in record */
    }
    
    ESP_LOGI(TAG, "Scan found %d networks", (int)s_wifi.scan_count);
    return ESP_OK;
}

esp_err_t wifi_manager_get_scan_results(const wifi_scan_result_t **results, 
                                         size_t *count)
{
    if (results == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *results = s_wifi.scan_results;
    *count = s_wifi.scan_count;
    return ESP_OK;
}

/* ============== IP Information ============== */

esp_err_t wifi_manager_get_ip(char *ip_addr, size_t len)
{
    if (!s_wifi.has_ip) {
        return ESP_ERR_INVALID_STATE;
    }
    
    strlcpy(ip_addr, inet_ntoa(s_wifi.ip_addr), len);
    return ESP_OK;
}

esp_err_t wifi_manager_get_ap_ip(char *ip_addr, size_t len)
{
    esp_netif_ip_info_t ip_info;
    esp_err_t err = esp_netif_get_ip_info(s_wifi.ap_netif, &ip_info);
    if (err != ESP_OK) {
        return err;
    }
    
    strlcpy(ip_addr, inet_ntoa(ip_info.ip), len);
    return ESP_OK;
}
