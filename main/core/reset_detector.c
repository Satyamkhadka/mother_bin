/**
 * @file reset_detector.c
 * @brief Factory reset detection via power-cycle counting and GPIO button
 * 
 * Detects factory reset triggers:
 * 1. Software reset counter: Counts ALL resets (power cycles, software reboots, etc.)
 *    Triggers reset when count reaches threshold within time window
 * 2. GPIO button: Hold button during boot to trigger immediate reset
 */

#include "reset_detector.h"
#include "dlm_config.h"
#include "storage/nvs_manager.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "reset_detector";

/* ============== Time Utilities ==============
 * ESP-IDF 5.5 doesn't have gettimeofday in early boot,
 * use esp_timer_get_time() which returns microseconds since boot
 */

static uint64_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000ULL;
}

/* ============== GPIO Button Handling ============== */

#if DLM_RESET_BUTTON_GPIO >= 0

static bool reset_button_pressed(void)
{
    /* Configure GPIO as input with pull-up (typical for active-low buttons) */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DLM_RESET_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return false;
    }
    
    /* Small delay for GPIO to settle */
    esp_rom_delay_us(100);
    
    int level = gpio_get_level(DLM_RESET_BUTTON_GPIO);
    
#if DLM_RESET_BUTTON_ACTIVE_LOW
    /* Button pulls line LOW when pressed (typical with pull-up) */
    return (level == 0);
#else
    /* Button pulls line HIGH when pressed */
    return (level == 1);
#endif
}

static bool reset_button_held_for_ms(uint32_t duration_ms)
{
    /* Check if button is initially pressed */
    if (!reset_button_pressed()) {
        return false;
    }
    
    ESP_LOGI(TAG, "Reset button pressed, waiting for %d ms...", (int)duration_ms);
    
    /* Wait and continuously check */
    uint32_t elapsed = 0;
    while (elapsed < duration_ms) {
        esp_rom_delay_us(10000);  /* 10ms */
        elapsed += 10;
        
        if (!reset_button_pressed()) {
            ESP_LOGI(TAG, "Reset button released early after %d ms", (int)elapsed);
            return false;
        }
    }
    
    ESP_LOGI(TAG, "Reset button held for %d ms - triggering!", (int)duration_ms);
    return true;
}

#endif /* DLM_RESET_BUTTON_GPIO >= 0 */

/* ============== Reset Counter Logic ============== */

typedef struct {
    uint8_t count;
    uint64_t first_reset_time_ms;
} reset_counter_t;

static esp_err_t load_counter(reset_counter_t *counter)
{
    size_t len = sizeof(reset_counter_t);
    esp_err_t err = nvs_manager_get_blob(DLM_RESET_NVS_NAMESPACE, 
                                          DLM_RESET_NVS_KEY_COUNT,
                                          counter, &len);
    
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Initialize new counter */
        counter->count = 0;
        counter->first_reset_time_ms = 0;
        return ESP_OK;
    }
    
    return err;
}

static esp_err_t save_counter(const reset_counter_t *counter)
{
    return nvs_manager_set_blob(DLM_RESET_NVS_NAMESPACE,
                                DLM_RESET_NVS_KEY_COUNT,
                                counter, sizeof(reset_counter_t));
}

static esp_err_t clear_counter(void)
{
    reset_counter_t counter = {0};
    return save_counter(&counter);
}

/* ============== Public API ============== */

bool reset_detector_triggered(void)
{
    /* Check GPIO button first (immediate reset) */
#if DLM_RESET_BUTTON_GPIO >= 0
    // if (reset_button_held_for_ms(DLM_RESET_BUTTON_DEBOUNCE_MS)) {
    //     ESP_LOGW(TAG, "GPIO reset button triggered!");
    //     return true;
    // }
#endif
    
    /* Check reset counter */
    reset_counter_t counter;
    esp_err_t err = load_counter(&counter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load reset counter: %s", esp_err_to_name(err));
        /* Continue anyway, might be first boot */
        counter.count = 0;
        counter.first_reset_time_ms = 0;
    }
    
    uint64_t now = get_time_ms();
    
    /* Check if we're within the time window */
    if (counter.count > 0) {
        uint64_t elapsed = now - counter.first_reset_time_ms;
        if (elapsed > DLM_FACTORY_RESET_WINDOW_MS) {
            /* Window expired, start fresh */
            ESP_LOGI(TAG, "Reset window expired (%d ms), resetting counter", 
                     (int)elapsed);
            counter.count = 0;
            counter.first_reset_time_ms = 0;
        }
    }
    
    /* Increment counter */
    if (counter.count == 0) {
        counter.first_reset_time_ms = now;
    }
    counter.count++;
    
    ESP_LOGI(TAG, "Reset count: %d/%d (window: %d ms remaining)",
             counter.count, DLM_FACTORY_RESET_THRESHOLD,
             (int)(DLM_FACTORY_RESET_WINDOW_MS - (now - counter.first_reset_time_ms)));
    
    /* Safety check: if count exceeds max, reset to prevent overflow/corruption issues */
    if (counter.count > DLM_FACTORY_RESET_MAX) {
        ESP_LOGW(TAG, "Reset count exceeded max (%d), clearing", DLM_FACTORY_RESET_MAX);
        clear_counter();
        return false;
    }
    
    /* Save updated counter */
    err = save_counter(&counter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save reset counter: %s", esp_err_to_name(err));
    }
    
    /* Check if threshold reached */
    if (counter.count >= DLM_FACTORY_RESET_THRESHOLD) {
        ESP_LOGW(TAG, "Reset threshold reached (%d/%d)!",
                 counter.count, DLM_FACTORY_RESET_THRESHOLD);
        return true;
    }
    
    return false;
}

void reset_detector_clear(void)
{
    ESP_LOGI(TAG, "Clearing reset counter");
    esp_err_t err = clear_counter();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear counter: %s", esp_err_to_name(err));
    }
}

void reset_detector_initiate_factory_reset(void)
{
    ESP_LOGW(TAG, "=========================================");
    ESP_LOGW(TAG, "  FACTORY RESET INITIATED");
    ESP_LOGW(TAG, "  Erasing all configuration...");
    ESP_LOGW(TAG, "=========================================");
    
    /* Clear reset counter first (so we don't trigger again on reboot) */
    reset_detector_clear();
    
    /* Clear WiFi credentials */
    nvs_manager_erase_namespace(DLM_WIFI_NVS_NAMESPACE);
    ESP_LOGI(TAG, "WiFi credentials erased");
    
    /* Clear device configuration */
    nvs_manager_erase_namespace(DLM_CONFIG_NVS_NAMESPACE);
    ESP_LOGI(TAG, "Device configuration erased");
    
    /* Clear OTA configuration */
    nvs_manager_erase_namespace(DLM_OTA_NVS_NAMESPACE);
    ESP_LOGI(TAG, "OTA configuration erased");
    
    /* Clear reset counter namespace */
    nvs_manager_erase_namespace(DLM_RESET_NVS_NAMESPACE);
    ESP_LOGI(TAG, "Reset counter erased");
    
    ESP_LOGW(TAG, "Factory reset complete!");
    ESP_LOGW(TAG, "Device will reboot to provisioning mode.");
    
    /* Set boot reason for app to read */
    nvs_manager_set_string(DLM_OTA_NVS_NAMESPACE, DLM_BOOT_REASON_NVS_KEY, 
                           DLM_BOOT_REASON_FACTORY_RESET);
    
    /* Give time for logs to flush */
    esp_rom_delay_us(500000);  /* 500ms */
}
