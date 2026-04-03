/**
 * @file main.c
 * @brief ESP32 Device Lifecycle Manager - Entry Point
 * 
 * This is the main entry point for the Device Lifecycle Manager (DLM).
 * It orchestrates the boot sequence:
 * 
 * 1. Initialize NVS
 * 2. Check factory reset conditions (button and reset counter)
 * 3. Initialize LED indicator
 * 4. Start boot manager (WiFi, provisioning, OTA, app boot)
 * 
 * The DLM runs first on every boot, handles provisioning, manages
 * OTA updates, and then boots the application firmware.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

/* DLM Modules */
#include "storage/nvs_manager.h"
#include "storage/config_store.h"
#include "core/boot_manager.h"
#include "core/reset_detector.h"
#include "platform/led_indicator.h"
#include "network/wifi_manager.h"
#include "network/http_server.h"
#include "ui/web_portal.h"

static const char *TAG = "DLM";

/* ============== Configuration ==============
 * These can be overridden via menuconfig or sdkconfig
 */

#ifndef CONFIG_DLM_LED_GPIO
#define CONFIG_DLM_LED_GPIO     2   /* GPIO2 - built-in LED on most dev boards */
#endif

#ifndef CONFIG_DLM_RESET_BUTTON_GPIO
#define CONFIG_DLM_RESET_BUTTON_GPIO    0   /* GPIO0 - boot button on most boards */
#endif

/* ============== Boot Complete Callback ============== */

static void boot_complete_handler(boot_result_t result, const char *message)
{
    ESP_LOGI(TAG, "Boot result: %d - %s", result, message);
    
    switch (result) {
        case BOOT_RESULT_SUCCESS:
            /* Application will boot - this handler may not return */
            ESP_LOGI(TAG, "Booting application...");
            break;
            
        case BOOT_RESULT_NEED_PROVISION:
            /* Waiting for user configuration via web portal */
            ESP_LOGI(TAG, "Waiting for provisioning...");
            /* LED pattern is already set by boot_manager */
            break;
            
        case BOOT_RESULT_UPDATING:
            /* OTA in progress, will reboot */
            ESP_LOGI(TAG, "Update in progress...");
            break;
            
        case BOOT_RESULT_ERROR:
            /* Fatal error - indicate and halt */
            ESP_LOGE(TAG, "Fatal error: %s", message);
            led_indicate_error();
            /* Could reboot after delay or wait for reset */
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
            break;
    }
}

/* ============== Main Entry Point ============== */

void app_main(void)
{
    /* Print banner */
    ESP_LOGI(TAG, "==============================================");
    ESP_LOGI(TAG, "  ESP32 Device Lifecycle Manager v%s", DLM_VERSION_STRING);
    ESP_LOGI(TAG, "==============================================");
    
    /* Initialize NVS (required for all storage operations) */
    esp_err_t err = nvs_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        /* Try to continue anyway - might work on next boot */
    }
    
    /* Check for factory reset via GPIO button or reset counter */
    /* This must happen early before we clear the reset counter */
    if (reset_detector_triggered()) {
        ESP_LOGW(TAG, "**********************************************");
        ESP_LOGW(TAG, "*       FACTORY RESET TRIGGERED              *");
        ESP_LOGW(TAG, "**********************************************");
        
        /* Perform factory reset */
        reset_detector_initiate_factory_reset();
        
        /* Reboot to start fresh */
        ESP_LOGI(TAG, "Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
        return;  /* Never reached */
    }
    
    /* Initialize LED indicator for visual feedback */
    err = led_indicator_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LED init failed: %s", esp_err_to_name(err));
        /* Continue without LED */
    } else {
        /* Quick LED test - solid on for 500ms then off */
        led_set_pattern(LED_PATTERN_ON);
        vTaskDelay(pdMS_TO_TICKS(500));
        led_set_pattern(LED_PATTERN_OFF);
    }
    
    /* Print system information */
    ESP_LOGI(TAG, "Chip: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "SDK Version: %s", esp_get_idf_version());
    
    /* Get reset reason */
    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %d", reset_reason);
    
    /* Get running partition info */
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "Running partition: %s", running->label);
    
    /* Initialize and start boot manager */
    err = boot_manager_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Boot manager init failed: %s", esp_err_to_name(err));
        led_indicate_error();
        return;
    }
    
    /* Start the boot sequence */
    err = boot_manager_start(boot_complete_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Boot sequence failed to start: %s", esp_err_to_name(err));
        led_indicate_error();
        return;
    }
    
    /* Main loop - handles web server and other tasks */
    /* The boot manager runs asynchronously, this loop keeps the task alive */
    while (1) {
        /* Update LED pattern based on boot phase */
        led_pattern_t pattern = boot_manager_get_led_pattern();
        if (led_get_pattern() != pattern) {
            led_set_pattern(pattern);
        }
        
        /* Check WiFi connection status */
        if (boot_manager_get_phase() == BOOT_PHASE_CONNECTING) {
            if (wifi_manager_is_connected()) {
                /* WiFi connected - boot manager will be notified via callback */
            }
        }
        
        /* Periodic tasks */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
