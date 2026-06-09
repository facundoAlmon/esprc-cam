#include "led_status.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

// AI-Thinker ESP32-CAM: onboard red LED on GPIO 33, active LOW.
#define LED_GPIO GPIO_NUM_33

static volatile led_state_t s_state = LED_BOOT;
static volatile uint32_t    s_ms    = 0;

static inline void set_led(bool on) {
    gpio_set_level(LED_GPIO, on ? 0 : 1);
}

static bool pattern_on(led_state_t st, uint32_t ms) {
    switch (st) {
    case LED_BOOT:      return (ms % 1000) < 500;  // 1 Hz slow blink
    case LED_RETRY:     return (ms % 200)  < 100;  // 5 Hz fast blink
    case LED_READY:     return true;                // solid ON
    case LED_STREAMING: return (ms % 500)  < 250;  // 2 Hz medium blink
    default:            return false;
    }
}

static void led_task(void*) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        s_ms += 50;
        set_led(pattern_on(s_state, s_ms));
    }
}

void led_status_init(void) {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << LED_GPIO);
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&io);
    set_led(false);  // LED off until task starts

    xTaskCreate(led_task, "led_status", 1024, NULL, 1, NULL);
}

void led_status_set(led_state_t s) {
    s_state = s;
}
