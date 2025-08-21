#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "command_bus.h"
#include "led.h"
#include "driver/gpio.h"
#include "esp_log.h"


#define LED_GPIO 22  // GPIO pin connected to the LED.

/**
 * @brief LED control task.
 * 
 * Waits for commands on the command bus and sets the LED state accordingly.
 * CMD_LED_ON => turns LED on.
 * CMD_LED_OFF => turns LED off.
 */
static void led_task(void *pv)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0); // Start with LED off.

    cmd_t cmd;
    while (cmd_bus_receive(&cmd, portMAX_DELAY)) {
        if (cmd == CMD_LED_ON)  gpio_set_level(LED_GPIO, 1);
        if (cmd == CMD_LED_OFF) gpio_set_level(LED_GPIO, 0);
    }
}

/**
 * @brief Launches the LED task.
 * 
 * Must be called once from app_main(), after cmd_bus_init().
 */
void led_task_start(void) {
    configASSERT(cmd_bus_is_ready());
    if (!cmd_bus_is_ready()) return; // or log and return.
    xTaskCreate(led_task, "led", 2048, NULL, 4, NULL);
}
