/**
 * system_init.c — System clock and GPIO initialization
 *
 * Extracted from main.c to be shared between:
 * - FSM main() (when AUDIO_TESTS_MAIN is not defined)
 * - Test harness main() (when AUDIO_TESTS_MAIN is defined)
 */

#include "stm32l4xx_hal.h"

/* Pin defines */
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_5

/**
 * SystemClock_Config() — Configure 80 MHz SYSCLK via HSI + PLL
 *
 * Clock tree:
 *   HSI16 (16 MHz)
 *   -> PLL: /1, x10, /2
 *   -> SYSCLK 80 MHz
 *   -> AHB, APB1, APB2 all /1
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState  = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;     /* 16 MHz */
    osc.PLL.PLLM      = 1;                     /* /1  -> 16 MHz   */
    osc.PLL.PLLN      = 10;                    /* x10 -> 160 MHz  */
    osc.PLL.PLLR      = RCC_PLLR_DIV2;         /* /2  -> 80 MHz   */
    osc.PLL.PLLP      = RCC_PLLP_DIV7;
    osc.PLL.PLLQ      = RCC_PLLQ_DIV2;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        while (1) {}  /* Error handler */
    }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) {
        while (1) {}  /* Error handler */
    }
}

/**
 * GPIO_Init() — Initialize GPIO for LEDs and buttons
 */
void GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* LD2 (PA5) — output push-pull */
    g.Pin   = LED_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &g);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);

    /* B1 (PC13) — input */
    g.Pin  = GPIO_PIN_13;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &g);
}
