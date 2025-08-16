#pragma once

/**
 * @brief Initializes the LED GPIO.
 * 
 * @param gpio GPIO pin number connected to the LED.
 */
void led_init(int gpio);

/**
 * @brief Turns the LED on.
 */
void led_on(void);

/**
 * @brief Turns the LED off.
 */
void led_off(void);

/**
 * @brief Starts the LED task.
 * 
 * This task listens to the command bus and toggles the LED
 * based on received CMD_LED_ON / CMD_LED_OFF commands.
 */
void led_task_start(void);
