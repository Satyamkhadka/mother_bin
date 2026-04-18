/**
 * @file boot_manager.c
 * @brief Boot sequence orchestration
 * 
 * Manages the complete boot flow:
 * 1. Check factory reset conditions
 * 2. Load/store configuration
 * 3. WiFi connection or AP mode
 * 4. OTA update check and download
 * 5. Boot application firmware
 */

#include "boot_manager.h"
#include "dlm_config.h"
#include "reset_detector.h"
#include "storage/nvs_manager.h"
#include "storage/config_store.h"
#include "platform/led_indicator.h"
#include "network/http_server.h"
#include "network/wifi_manager.h"
#include "network/dns_server.h"
#include "ui/web_portal.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "mdns.h"

/* WiFi manager functions now declared in wifi_manager.h */

/* Forward declarations for OTA module */
/* These will be implemented in Phase 4 */
esp_err_t ota_manager_init(void);
esp_err_t ota_manager_check_update(bool *update_available);
esp_err_t ota_manager_perform_update(void (*progress_cb)(int percent));

static const char *TAG = "boot_manager";

/* ============== State ============== */

static struct {
    boot_phase_t phase;
    boot_complete_cb_t callback;
    bool initialized;
    bool is_first_boot;
    bool provisioning_entered;  /* Prevent double entry */
} s_boot = {
    .phase = BOOT_PHASE_INIT,
    .callback = NULL,
    .initialized = false,
    .is_first_boot = false,
    .provisioning_entered = false
};

/* ============== Phase String Mapping ============== */

static const char* s_phase_strs[] = {
    "INIT",
    "CHECK_RESET",
    "LOAD_CONFIG",
    "WIFI_INIT",
    "PROVISIONING",
    "CONNECTING",
    "CONNECTED",
    "OTA_CHECK",
    "OTA_UPDATE",
    "APP_BOOT",
    "ERROR"
};

const char* boot_manager_get_phase_str(void)
{
    if (s_boot.phase >= ARRAY_SIZE(s_phase_strs)) {
        return "UNKNOWN";
    }
    return s_phase_strs[s_boot.phase];
}

boot_phase_t boot_manager_get_phase(void)
{
    return s_boot.phase;
}

/* ============== State Transition ============== */

static void set_phase(boot_phase_t phase)
{
    s_boot.phase = phase;
    ESP_LOGI(TAG, "Boot phase: %s", boot_manager_get_phase_str());
    
    /* Update LED to match phase */
    led_pattern_t pattern = boot_manager_get_led_pattern();
    led_set_pattern(pattern);
}

/* ============== Factory Reset Handling ============== */

static esp_err_t handle_factory_reset(void)
{
    set_phase(BOOT_PHASE_CHECK_RESET);
    
    if (reset_detector_triggered()) {
        ESP_LOGW(TAG, "Factory reset triggered!");
        
        /* Perform reset */
        reset_detector_initiate_factory_reset();
        
        /* Will reboot after this - mark as first boot */
        s_boot.is_first_boot = true;
        
        /* Don't clear counter yet - let the reboot do that */
        return ESP_FAIL;  /* Will be handled as reboot */
    }
    
    ESP_LOGI(TAG, "No factory reset triggered");
    return ESP_OK;
}

/* ============== Configuration Loading ============== */

static esp_err_t load_configuration(void)
{
    set_phase(BOOT_PHASE_LOAD_CONFIG);
    
    /* Check if we have WiFi credentials */
    if (!wifi_manager_has_credentials()) {
        ESP_LOGI(TAG, "No WiFi credentials found - first boot");
        s_boot.is_first_boot = true;
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Initialize config store */
    esp_err_t err = config_store_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config store init failed: %s", esp_err_to_name(err));
        /* Continue anyway - we can still boot */
    }
    
    ESP_LOGI(TAG, "Configuration loaded");
    return ESP_OK;
}

/* ============== WiFi Handling ============== */

static void enter_provisioning_mode(void)
{
    /* Prevent duplicate entry */
    if (s_boot.provisioning_entered) {
        return;
    }
    s_boot.provisioning_entered = true;
    
    set_phase(BOOT_PHASE_PROVISIONING);
    ESP_LOGI(TAG, "Entering provisioning mode (AP mode)");
    
    /* Start AP mode with captive portal */
    esp_err_t err = wifi_manager_start_ap();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP mode: %s", esp_err_to_name(err));
        set_phase(BOOT_PHASE_ERROR);
        if (s_boot.callback) {
            s_boot.callback(BOOT_RESULT_ERROR, "Failed to start AP mode");
        }
        return;
    }
    
    /* Start HTTP server for captive portal */
    err = http_server_init(DLM_HTTP_PORT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        set_phase(BOOT_PHASE_ERROR);
        return;
    }
    
    /* Initialize web portal handlers */
    err = web_portal_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init web portal: %s", esp_err_to_name(err));
        set_phase(BOOT_PHASE_ERROR);
        return;
    }
    
    /* Notify callback that we need provisioning */
    if (s_boot.callback) {
        s_boot.callback(BOOT_RESULT_NEED_PROVISION, 
                       "Connect to WiFi and configure device");
    }
}

static void start_wifi_connection(void)
{
    set_phase(BOOT_PHASE_CONNECTING);
    
    char ssid[33] = {0};
    char password[65] = {0};
    
    esp_err_t err = wifi_manager_get_credentials(ssid, sizeof(ssid),
                                                  password, sizeof(password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get credentials: %s", esp_err_to_name(err));
        enter_provisioning_mode();
        return;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
    ESP_LOGI(TAG, "Connecting to WiFi pass: %s", password);
    
    /* Keep AP up so 192.168.4.1 remains accessible after STA connects */
    wifi_manager_start_ap();
    
    err = wifi_manager_connect(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(err));
        /* Fall back to provisioning */
        enter_provisioning_mode();
        return;
    }
    
    /* Connection started - wait for result with timeout */
    /* Connection result will be handled by wifi event callbacks */
    
    /* Start timeout timer to detect connection hangs */
    uint32_t elapsed = 0;
    while (elapsed < DLM_WIFI_CONNECT_TIMEOUT_SEC * 1000) {
        /* Check if phase changed (connected or failed) */
        if (s_boot.phase != BOOT_PHASE_CONNECTING) {
            return;  /* Event handler took over */
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }
    
    /* Timeout - connection failed */
    ESP_LOGW(TAG, "WiFi connection timeout");
    wifi_manager_disconnect();
    enter_provisioning_mode();
}

/* ============== OTA Handling ============== */

static void check_for_update(void)
{
    set_phase(BOOT_PHASE_OTA_CHECK);
    
    /* Initialize OTA manager */
    esp_err_t err = ota_manager_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA init failed: %s", esp_err_to_name(err));
        /* Continue without OTA - boot app directly */
        boot_manager_boot_application();
        return;
    }
    
    /* Check for available update */
    bool update_available = false;
    err = ota_manager_check_update(&update_available);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA check failed: %s", esp_err_to_name(err));
        /* Continue without update */
        boot_manager_boot_application();
        return;
    }
    
    if (!update_available) {
        ESP_LOGI(TAG, "No update available");
        boot_manager_boot_application();
        return;
    }
    
    /* Update available - download and install */
    set_phase(BOOT_PHASE_OTA_UPDATE);
    ESP_LOGI(TAG, "Update available, downloading...");
    
    /* Progress callback would update LED or status */
    err = ota_manager_perform_update(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
        /* Continue with current firmware */
        boot_manager_boot_application();
        return;
    }
    
    /* Update successful - will reboot */
    ESP_LOGI(TAG, "OTA complete, rebooting...");
    if (s_boot.callback) {
        s_boot.callback(BOOT_RESULT_UPDATING, "Update installed, rebooting...");
    }
    
    /* Set boot reason for app */
    nvs_manager_set_string(DLM_OTA_NVS_NAMESPACE, DLM_BOOT_REASON_NVS_KEY,
                           DLM_BOOT_REASON_AFTER_UPDATE);
    
    /* Set update pending flag for application */
    nvs_manager_set_uint8(DLM_OTA_NVS_NAMESPACE, DLM_OTA_NVS_KEY_UPDATE_PENDING, 1);
    
    /* Delay for callback then reboot */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

/* ============== Public API ============== */

esp_err_t boot_manager_init(void)
{
    if (s_boot.initialized) {
        return ESP_OK;
    }
    
    /* Initialize LED first for visual feedback */
    led_indicator_init();
    
    s_boot.initialized = true;
    ESP_LOGI(TAG, "Boot manager initialized");
    return ESP_OK;
}

esp_err_t boot_manager_start(boot_complete_cb_t callback)
{
    if (!s_boot.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_boot.callback = callback;
    
    /* Step 1: Check factory reset */
    if (handle_factory_reset() != ESP_OK) {
        /* Factory reset triggered or error - will reboot */
        ESP_LOGI(TAG, "Rebooting after factory reset...");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return ESP_OK;  /* Never reached */
    }
    
    /* Step 2: Initialize WiFi manager */
    set_phase(BOOT_PHASE_WIFI_INIT);
    esp_err_t err = wifi_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
        set_phase(BOOT_PHASE_ERROR);
        if (callback) {
            callback(BOOT_RESULT_ERROR, "WiFi initialization failed");
        }
        return err;
    }
    
    /* Step 3: Load configuration */
    err = load_configuration();
    if (err == ESP_ERR_NOT_FOUND) {
        /* No credentials - need provisioning */
        enter_provisioning_mode();
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Config load error: %s", esp_err_to_name(err));
        /* Continue anyway */
    }
    
    /* Step 4: Connect to WiFi */
    start_wifi_connection();
    
    return ESP_OK;
}

esp_err_t boot_manager_provisioning_complete(const char *ssid, const char *password)
{
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Provisioning complete, saving credentials for: %s", ssid);
    
    /* Save credentials */
    esp_err_t err = wifi_manager_save_credentials(ssid, password ? password : "");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save credentials: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Clear reset counter - device is now configured */
    reset_detector_clear();
    
    /* Mark as first boot complete */
    s_boot.is_first_boot = false;
    
    /* Now try to connect */
    start_wifi_connection();
    
    return ESP_OK;
}

void boot_manager_retry_connection(void)
{
    if (s_boot.phase == BOOT_PHASE_PROVISIONING) {
        ESP_LOGW(TAG, "Already in provisioning mode");
        return;
    }
    
    ESP_LOGI(TAG, "Retrying WiFi connection...");
    start_wifi_connection();
}

void boot_manager_node_config_complete(void)
{
    if (s_boot.phase == BOOT_PHASE_CONNECTED) {
        ESP_LOGI(TAG, "Node configuration complete, proceeding to OTA check");
        check_for_update();
    }
}

bool boot_manager_has_application(void)
{
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        return false;
    }
    
    /* Check if partition has valid app */
    esp_app_desc_t app_desc;
    esp_err_t err = esp_ota_get_partition_description(ota_partition, &app_desc);
    if (err != ESP_OK) {
        return false;
    }
    
    /* Check magic number */
    return (app_desc.magic_word == ESP_APP_DESC_MAGIC_WORD);
}

esp_err_t boot_manager_boot_application(void)
{
    set_phase(BOOT_PHASE_APP_BOOT);
    
    /* Check if we have an application to boot */
    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    if (ota_partition == NULL) {
        ESP_LOGW(TAG, "No OTA partition found, staying in DLM");
        goto stay_in_dlm;
    }
    
    /* Verify app is valid */
    esp_app_desc_t app_desc;
    esp_err_t err = esp_ota_get_partition_description(ota_partition, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No valid app in %s, staying in DLM", ota_partition->label);
        goto stay_in_dlm;
    }
    
    ESP_LOGI(TAG, "Booting application from partition: %s", ota_partition->label);
    
    ESP_LOGI(TAG, "App version: %s", app_desc.version);
    ESP_LOGI(TAG, "App name: %s", app_desc.project_name);
    
    /* Set boot partition */
    err = esp_ota_set_boot_partition(ota_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        set_phase(BOOT_PHASE_ERROR);
        if (s_boot.callback) {
            s_boot.callback(BOOT_RESULT_ERROR, "Failed to set boot partition");
        }
        return err;
    }
    
    /* Set boot reason for app to read */
    const char *reason = s_boot.is_first_boot ? DLM_BOOT_REASON_FIRST_BOOT 
                                               : DLM_BOOT_REASON_NORMAL;
    nvs_manager_set_string(DLM_OTA_NVS_NAMESPACE, DLM_BOOT_REASON_NVS_KEY, reason);
    
    /* Notify callback */
    if (s_boot.callback) {
        s_boot.callback(BOOT_RESULT_SUCCESS, "Booting application...");
    }
    
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  BOOTING APPLICATION - DLM HANDOFF");
    ESP_LOGI(TAG, "============================================");
    
    /* Small delay for logs */
    vTaskDelay(pdMS_TO_TICKS(DLM_BOOT_DELAY_MS));
    
    /* Boot the application - does not return on success */
    esp_restart();
    
stay_in_dlm:
    /* No valid application — keep DLM running with web interface accessible */
    ESP_LOGI(TAG, "DLM interface active");
    
    esp_err_t srv_err = http_server_init(DLM_HTTP_PORT);
    if (srv_err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP server init failed: %s", esp_err_to_name(srv_err));
    }
    
    srv_err = web_portal_init();
    if (srv_err != ESP_OK) {
        ESP_LOGW(TAG, "Web portal init failed: %s", esp_err_to_name(srv_err));
    }
    
    set_phase(BOOT_PHASE_CONNECTED);
    return ESP_OK;
}

led_pattern_t boot_manager_get_led_pattern(void)
{
    switch (s_boot.phase) {
        case BOOT_PHASE_INIT:
        case BOOT_PHASE_CHECK_RESET:
        case BOOT_PHASE_LOAD_CONFIG:
        case BOOT_PHASE_WIFI_INIT:
            return LED_PATTERN_ON;  /* Solid during init */
            
        case BOOT_PHASE_PROVISIONING:
            return LED_PATTERN_SLOW_BLINK;  /* Waiting for user */
            
        case BOOT_PHASE_CONNECTING:
            return LED_PATTERN_FAST_BLINK;  /* Actively connecting */
            
        case BOOT_PHASE_CONNECTED:
            return LED_PATTERN_DOUBLE_BLINK;  /* Connected */
            
        case BOOT_PHASE_OTA_CHECK:
        case BOOT_PHASE_OTA_UPDATE:
            return LED_PATTERN_TRIPLE_BLINK;  /* Updating */
            
        case BOOT_PHASE_APP_BOOT:
            return LED_PATTERN_ON;  /* Solid before handoff */
            
        case BOOT_PHASE_ERROR:
            return LED_PATTERN_ERROR;  /* Rapid flash */
            
        default:
            return LED_PATTERN_OFF;
    }
}

/* ============== WiFi Event Callback ==============
 * Called by wifi_manager when connection status changes
 */
static void delayed_ap_stop_task(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(20000));
    wifi_manager_stop_ap();
    dns_server_stop();
    ESP_LOGI("boot_manager", "AP stopped after grace period");
    vTaskDelete(NULL);
}

void boot_manager_on_wifi_connected(void)
{
    if (s_boot.phase == BOOT_PHASE_CONNECTING || 
        s_boot.phase == BOOT_PHASE_PROVISIONING) {
        
        set_phase(BOOT_PHASE_CONNECTED);
        ESP_LOGI(TAG, "WiFi connected!");
        
        /* Start mDNS for local access */
        esp_err_t err = mdns_init();
        if (err == ESP_OK) {
            mdns_hostname_set(DLM_MDNS_HOSTNAME);
            mdns_instance_name_set("ESP Life Manager");
            mdns_service_add(NULL, "_http", "_tcp", DLM_HTTP_PORT, NULL, 0);
            ESP_LOGI(TAG, "mDNS started: http://%s.local", DLM_MDNS_HOSTNAME);
        } else {
            ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        }
        
        /* Ensure HTTP server is running for web portal access */
        esp_err_t srv_err = http_server_init(DLM_HTTP_PORT);
        if (srv_err != ESP_OK) {
            ESP_LOGW(TAG, "HTTP server init failed: %s", esp_err_to_name(srv_err));
        }
        
        srv_err = web_portal_init();
        if (srv_err != ESP_OK) {
            ESP_LOGW(TAG, "Web portal init failed: %s", esp_err_to_name(srv_err));
        }
        
        /* Delay AP shutdown so user sees transition message */
        xTaskCreate(delayed_ap_stop_task, "ap_stop_delay", 2048, NULL, 5, NULL);
        
        /* Clear reset counter - successful connection */
        reset_detector_clear();
        
        /* Check if node is already configured */
        char node_id[64] = {0};
        err = config_store_get_string("node_id", node_id, sizeof(node_id));
        if (err == ESP_OK && strlen(node_id) > 0) {
            ESP_LOGI(TAG, "Node configured (%s), proceeding to OTA", node_id);
            check_for_update();
        } else {
            ESP_LOGI(TAG, "WiFi ready. Visit http://%s.local to configure node", DLM_MDNS_HOSTNAME);
        }
    }
}

void boot_manager_on_wifi_failed(void)
{
    ESP_LOGW(TAG, "WiFi connection failed");
    
    /* Only handle if we're still in connecting phase */
    if (s_boot.phase == BOOT_PHASE_CONNECTING) {
        enter_provisioning_mode();
    }
}
