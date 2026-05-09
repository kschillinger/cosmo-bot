/**
 * debug_uart.c — USART2 (PA2/PA3) at 115200, blocking TX, retargeted printf.
 */

#include "debug_uart.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <unistd.h>

static UART_HandleTypeDef huart2;

void DebugUart_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    /* PA2 = USART2_TX, PA3 = USART2_RX, AF7 */
    g.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &g);

    huart2.Instance                    = USART2;
    huart2.Init.BaudRate               = 115200;
    huart2.Init.WordLength             = UART_WORDLENGTH_8B;
    huart2.Init.StopBits               = UART_STOPBITS_1;
    huart2.Init.Parity                 = UART_PARITY_NONE;
    huart2.Init.Mode                   = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart2) != HAL_OK) {
        while (1) {}
    }
}

/* ---- newlib-nano printf retarget ---------------------------------------- */
/* PlatformIO's stm32cube + newlib calls _write() for printf/puts. */
int _write(int file, char* ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart2, (uint8_t*)ptr, (uint16_t)len, 100);
    return len;
}

/* Some newlib configs prefer __io_putchar — provide it too, harmless. */
int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart2, &c, 1, 100);
    return ch;
}
