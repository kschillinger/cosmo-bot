/**
 * system_utils.c — HAL wrappers + B1 edge detection.
 */

#include "system_utils.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>

/* B1 user button on Nucleo-L476RG: PC13, active low (external pull-up). */
#define BUTTON_PORT     GPIOC
#define BUTTON_PIN      GPIO_PIN_13
#define DEBOUNCE_MS     20U

uint32_t system_get_tick_ms(void)
{
    return HAL_GetTick();
}

void system_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

uint8_t system_button_pressed_edge(void)
{
    static uint8_t  last_pressed = 0;
    static uint32_t last_change_ms = 0;

    uint8_t now_pressed =
        (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN) == GPIO_PIN_RESET) ? 1U : 0U;

    uint32_t now = HAL_GetTick();

    if (now_pressed != last_pressed) {
        /* Hold off until stable for DEBOUNCE_MS. */
        if ((now - last_change_ms) < DEBOUNCE_MS) {
            return 0;
        }
        last_change_ms = now;
        last_pressed   = now_pressed;
        if (now_pressed) {
            printf("[SYS] button edge (B1 pressed)\r\n");
            return 1;
        }
    }
    return 0;
}
