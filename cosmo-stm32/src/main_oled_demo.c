/**
 * main_oled_demo.c — standalone OLED bring-up demo (SSD1306 I2C).
 */

#include "stm32l4xx_hal.h"
#include "oled_display.h"
#include "system_utils.h"

#include <stdio.h>

static void SystemClock_Config(void);
static void GPIO_Init(void);
static void fill_demo_waveform(uint16_t *samples, uint16_t count, uint16_t frame);
void Error_Handler(void);

#define DEMO_SAMPLE_COUNT 128U

int main(void)
{
    uint16_t samples[DEMO_SAMPLE_COUNT];
    uint16_t frame = 0U;
    uint32_t state_start = 0U;

    HAL_Init();
    SystemClock_Config();
    GPIO_Init();

    printf("\r\n[OLED-DEMO] boot\r\n");
    oled_init();

    for (;;) {
        uint32_t elapsed = system_get_tick_ms() - state_start;

        if (elapsed < 3000U) {
            oled_display_idle();
        } else if (elapsed < 6000U) {
            fill_demo_waveform(samples, DEMO_SAMPLE_COUNT, frame);
            oled_display_listening(samples, DEMO_SAMPLE_COUNT);
        } else if (elapsed < 9000U) {
            oled_display_processing(frame);
        } else if (elapsed < 14000U) {
            oled_display_responding("OLED demo", frame);
        } else {
            state_start = system_get_tick_ms();
            frame = 0U;
            continue;
        }

        oled_update_display();
        frame++;
        system_delay_ms(100);
    }
}

static void fill_demo_waveform(uint16_t *samples, uint16_t count, uint16_t frame)
{
    uint16_t i;

    for (i = 0U; i < count; ++i) {
        uint16_t saw = (uint16_t)((i * 512U + frame * 96U) & 0x0FFFU);
        uint16_t tri = (saw < 2048U) ? saw : (uint16_t)(4095U - saw);
        samples[i] = (uint16_t)(tri << 4U);
    }
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState  = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM      = 1;
    osc.PLL.PLLN      = 10;
    osc.PLL.PLLR      = RCC_PLLR_DIV2;
    osc.PLL.PLLP      = RCC_PLLP_DIV7;
    osc.PLL.PLLQ      = RCC_PLLQ_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
}

static void GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    g.Pin   = GPIO_PIN_5;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

    g.Pin  = GPIO_PIN_13;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &g);
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void NMI_Handler(void)        { while (1) {} }
void HardFault_Handler(void)  { while (1) {} }
void MemManage_Handler(void)  { while (1) {} }
void BusFault_Handler(void)   { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void)        {}
void DebugMon_Handler(void)   {}
void PendSV_Handler(void)     {}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(200);
    }
}
