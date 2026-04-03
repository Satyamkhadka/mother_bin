/**
 * @file led_indicator.c
 * @brief LED indicator with pattern support
 * 
 * Supports various blinking patterns to indicate system state
 * using a single LED. Patterns can be extended for future needs.
 * 
 * NOTE: This module uses simple GPIO toggling. For PWM-based
 * patterns like breathing, hardware PWM or LEDC would be needed.
 */

#include "led_indicator.h"
#include "dlm_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "led_indicator";

/* ============== Pattern Definitions ==============
 * Each pattern is a sequence of on/off times in milliseconds
 * A period of 0 indicates end of sequence
 */

typedef struct {
    uint32_t on_ms;
    uint32_t off_ms;
    uint32_t repeat_count;  /* 0 = infinite */
} led_pattern_step_t;

/* Pattern table - defines the blinking sequences */
static const led_pattern_step_t s_patterns[][4] = {
    /* LED_PATTERN_OFF */
    {{0, 1000, 1}, {0, 0, 0}},
    
    /* LED_PATTERN_ON */
    {{1000, 0, 1}, {0, 0, 0}},
    
    /* LED_PATTERN_SLOW_BLINK - 1Hz (500ms on, 500ms off) */
    {{500, 500, 0}, {0, 0, 0}},
    
    /* LED_PATTERN_FAST_BLINK - 4Hz (125ms on, 125ms off) */
    {{125, 125, 0}, {0, 0, 0}},
    
    /* LED_PATTERN_DOUBLE_BLINK - Two quick blinks, then pause */
    {{100, 100, 2}, {500, 0, 1}, {0, 0, 0}},
    
    /* LED_PATTERN_TRIPLE_BLINK - Three quick blinks, then pause */
    {{100, 100, 3}, {500, 0, 1}, {0, 0, 0}},
    
    /* LED_PATTERN_BREATHING - Simulated with stepped PWM (not true PWM) */
    /* NOTE: For real breathing, LEDC PWM hardware is needed */
    {{50, 50, 1}, {100, 50, 1}, {50, 500, 1}, {0, 0, 0}},
    
    /* LED_PATTERN_ERROR - Rapid flashing (100ms on, 100ms off) */
    {{100, 100, 0}, {0, 0, 0}},
};

/* ============== State ============== */

static struct {
    bool initialized;
    led_pattern_t current_pattern;
    TaskHandle_t task_handle;
    volatile bool running;
    bool led_state;  /* Current GPIO state */
} s_led = {0};

/* ============== Hardware Control ============== */

static inline void led_set(bool on)
{
    if (!s_led.initialized) return;
    
#if DLM_LED_ACTIVE_HIGH
    gpio_set_level(DLM_LED_GPIO, on ? 1 : 0);
#else
    gpio_set_level(DLM_LED_GPIO, on ? 0 : 1);
#endif
    s_led.led_state = on;
}

static inline void led_on(void)
{
    led_set(true);
}

static inline void led_off(void)
{
    led_set(false);
}

static inline void led_toggle(void)
{
    led_set(!s_led.led_state);
}

/* ============== Pattern Task ============== */

static void led_pattern_task(void *pvParameters)
{
    (void)pvParameters;
    
    led_pattern_t current = LED_PATTERN_OFF;
    uint32_t step_index = 0;
    uint32_t repeat_counter = 0;
    
    while (s_led.running) {
        /* Check if pattern changed */
        if (s_led.current_pattern != current) {
            current = s_led.current_pattern;
            step_index = 0;
            repeat_counter = 0;
            ESP_LOGD(TAG, "Pattern changed to %d", current);
        }
        
        if (current >= ARRAY_SIZE(s_patterns)) {
            current = LED_PATTERN_OFF;
        }
        
        const led_pattern_step_t *step = &s_patterns[current][step_index];
        
        /* Check for end of sequence */
        if (step->on_ms == 0 && step->off_ms == 0) {
            /* End of sequence, start over */
            step_index = 0;
            repeat_counter = 0;
            continue;
        }
        
        /* Execute step */
        if (step->on_ms > 0) {
            led_on();
            vTaskDelay(pdMS_TO_TICKS(step->on_ms));
        }
        
        if (step->off_ms > 0) {
            led_off();
            vTaskDelay(pdMS_TO_TICKS(step->off_ms));
        }
        
        /* Handle repeats */
        if (step->repeat_count > 0) {
            repeat_counter++;
            if (repeat_counter >= step->repeat_count) {
                repeat_counter = 0;
                step_index++;
            }
            /* else: repeat same step */
        } else {
            /* Infinite repeat of this step */
            /* step_index stays the same */
        }
    }
    
    /* Task ending */
    led_off();
    s_led.task_handle = NULL;
    vTaskDelete(NULL);
}

/* ============== Public API ============== */

esp_err_t led_indicator_init(void)
{
#if DLM_LED_GPIO < 0
    ESP_LOGW(TAG, "LED support disabled (GPIO not configured)");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    if (s_led.initialized) {
        return ESP_OK;
    }

    /* Configure GPIO */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DLM_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Start with LED off */
    led_off();
    
    s_led.initialized = true;
    s_led.running = true;
    s_led.current_pattern = LED_PATTERN_OFF;
    
    /* Create pattern task */
    BaseType_t ret = xTaskCreate(
        led_pattern_task,
        "led_task",
        2048,
        NULL,
        1,  /* Low priority */
        &s_led.task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED task");
        s_led.initialized = false;
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "LED initialized on GPIO %d", DLM_LED_GPIO);
    return ESP_OK;
}

void led_indicator_deinit(void)
{
    if (!s_led.initialized) {
        return;
    }
    
    s_led.running = false;
    
    /* Wait for task to finish */
    if (s_led.task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    led_off();
    s_led.initialized = false;
}

void led_set_pattern(led_pattern_t pattern)
{
    if (!s_led.initialized) {
        return;
    }
    
    if (pattern >= ARRAY_SIZE(s_patterns)) {
        pattern = LED_PATTERN_OFF;
    }
    
    s_led.current_pattern = pattern;
    
    /* Log pattern change for debugging */
    const char *pattern_name = "unknown";
    switch (pattern) {
        case LED_PATTERN_OFF:           pattern_name = "OFF"; break;
        case LED_PATTERN_ON:            pattern_name = "ON"; break;
        case LED_PATTERN_SLOW_BLINK:    pattern_name = "SLOW_BLINK"; break;
        case LED_PATTERN_FAST_BLINK:    pattern_name = "FAST_BLINK"; break;
        case LED_PATTERN_DOUBLE_BLINK:  pattern_name = "DOUBLE_BLINK"; break;
        case LED_PATTERN_TRIPLE_BLINK:  pattern_name = "TRIPLE_BLINK"; break;
        case LED_PATTERN_BREATHING:     pattern_name = "BREATHING"; break;
        case LED_PATTERN_ERROR:         pattern_name = "ERROR"; break;
    }
    
    ESP_LOGD(TAG, "Pattern set to %s", pattern_name);
}

led_pattern_t led_get_pattern(void)
{
    return s_led.current_pattern;
}

void led_on_static(void)
{
    if (s_led.initialized) {
        led_on();
    }
}

void led_off_static(void)
{
    if (s_led.initialized) {
        led_off();
    }
}

/* ============== State-to-Pattern Mapping ==============
 * Helper functions to set appropriate patterns for system states
 */

void led_indicate_provisioning(void)
{
    /* Slow blink = waiting for configuration */
    led_set_pattern(LED_PATTERN_SLOW_BLINK);
}

void led_indicate_connecting(void)
{
    /* Fast blink = actively connecting */
    led_set_pattern(LED_PATTERN_FAST_BLINK);
}

void led_indicate_connected(void)
{
    /* Double blink = connected and operational */
    led_set_pattern(LED_PATTERN_DOUBLE_BLINK);
}

void led_indicate_updating(void)
{
    /* Triple blink = OTA in progress */
    led_set_pattern(LED_PATTERN_TRIPLE_BLINK);
}

void led_indicate_error(void)
{
    /* Rapid blink = error state */
    led_set_pattern(LED_PATTERN_ERROR);
}

void led_indicate_success(void)
{
    /* Solid on for 2 seconds, then back to connected pattern */
    led_set_pattern(LED_PATTERN_ON);
    
    /* NOTE: In a real implementation, you might want a timer
     * to automatically return to LED_PATTERN_DOUBLE_BLINK after 2s
     * For now, caller should manage this transition */
}
