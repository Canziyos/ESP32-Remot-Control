// components/led/led.c
#include "led.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifndef CONFIG_APP_LED_GPIO
#define CONFIG_APP_LED_GPIO 22
#endif
#ifndef CONFIG_APP_LED_ACTIVE_HIGH
#define CONFIG_APP_LED_ACTIVE_HIGH 1
#endif

static int s_led_gpio = CONFIG_APP_LED_GPIO;

esp_err_t led_init(void) {
    gpio_reset_pin(s_led_gpio);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_led_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t e = gpio_config(&io);
#if CONFIG_APP_LED_ACTIVE_HIGH
    gpio_set_level(s_led_gpio, 0);
#else
    gpio_set_level(s_led_gpio, 1);
#endif
    return e;
}

void led_on(void) {
#if CONFIG_APP_LED_ACTIVE_HIGH
    gpio_set_level(s_led_gpio, 1);
#else
    gpio_set_level(s_led_gpio, 0);
#endif
}
void led_off(void) {
#if CONFIG_APP_LED_ACTIVE_HIGH
    gpio_set_level(s_led_gpio, 0);
#else
    gpio_set_level(s_led_gpio, 1);
#endif
}
