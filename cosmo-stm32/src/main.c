/**
 * Cosmo Bot — STM32L476RG (Nucleo-64)
 * ============================================================================
 * Phase 1: UART link bring-up
 *
 * Responsibilities (this phase):
 *   - Configure 80 MHz SYSCLK from HSI16 + PLL
 *   - Init USART2 for printf debug via ST-Link VCP
 *   - Init USART1 for protocol link to ESP32-C3
 *   - On user button press (B1 / PC13): send "[USER_INPUT: test message]\r\n"
 *   - On incoming "[BOT_RESPONSE: ...]\r\n" from ESP32: log it & toggle LD2
 *
 * Pin map (Nucleo-L476RG):
 *   PA2  USART2_TX  -> ST-Link VCP (printf debug)
 *   PA3  USART2_RX  <- ST-Link VCP
 *   PA5  LD2 (green LED, output)
 *   PA9  USART1_TX  -> ESP32 GPIO5 (Serial1 RX)    [CN10-21]
 *   PA10 USART1_RX  <- ESP32 GPIO4 (Serial1 TX)    [CN10-33]
 *   PC13 B1 user button (input, active low — board has external pull-up)
 *
 * Build / flash:
 *   pio run -e nucleo_l476rg -t upload
 *   pio device monitor -e nucleo_l476rg
 * ============================================================================
 */

#include "stm32l4xx_hal.h"
#include "uart_link.h"
#include "debug_uart.h"
#ifdef OLED_DEMO
#include "display/oled_display.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ---- Pin defines --------------------------------------------------------- */
#define BUTTON_PORT     GPIOC
#define BUTTON_PIN      GPIO_PIN_13
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_5

/* ---- Forward declarations ------------------------------------------------ */
static void SystemClock_Config(void);
static void GPIO_Init(void);
void Error_Handler(void);

#ifdef OLED_DEMO
static HAL_StatusTypeDef I2C1_InitPins(uint16_t scl_pin, uint16_t sda_pin, const char *label);
static uint8_t OLED_ProbeAddress(uint8_t *addr_out);
static void OLED_DemoLoop(void);
static void OLED_ErrorBlink(uint32_t delay_ms);

static I2C_HandleTypeDef hi2c1;
static const char *s_oled_pinset_label = "I2C1 PB8/PB9";
#endif

/* ========================================================================== */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();

#ifdef OLED_DEMO
    DebugUart_Init();

    printf("\r\n");
    printf("==========================================\r\n");
    printf(" OLED Demo — STM32L476RG\r\n");
    printf(" SYSCLK = %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("==========================================\r\n");

    OLED_DemoLoop();
#else
    DebugUart_Init();
    UartLink_Init();

    printf("\r\n");
    printf("==========================================\r\n");
    printf(" Cosmo Bot — STM32L476RG  Phase 1\r\n");
    printf(" SYreLK = %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    printf(" Press B1 (blue button) to send a test\r\n");
    printf(" USER_INPUT to the ESP32-C3.\r\n");
    printf("==========================================\r\n");

    char rx_buffer[256];
    GPIO_PinState last_button = GPIO_PIN_SET;  /* not pressed */

    while (1) {
        /* Poll button (active low; ~20 ms debounce) */
        GPIO_PinState now = HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN);
        if (now == GPIO_PIN_RESET && last_button == GPIO_PIN_SET) {
            HAL_Delay(20);
            if (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN) == GPIO_PIN_RESET) {
                printf("[STM32] button -> sending USER_INPUT\r\n");
                UartLink_SendUserInput("test message");
            }
        }
        last_button = now;

        /* Drain any complete BOT_RESPONSE lines from the link */
        if (UartLink_PollBotResponse(rx_buffer, sizeof(rx_buffer))) {
            printf("[STM32] <- BOT_RESPONSE: %s\r\n", rx_buffer);
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        }

        HAL_Delay(5);
    }
#endif
}

/* ========================================================================== */
/* Clock: HSI16 -> PLL -> 80 MHz SYSCLK                                       */
/* ========================================================================== */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    /* Voltage scale 1 required for 80 MHz */
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
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
}

/* ========================================================================== */
static void GPIO_Init(void)
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

    /* B1 (PC13) — input. Board has an external pull-up, so NOPULL. */
    g.Pin  = BUTTON_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BUTTON_PORT, &g);
}

#ifdef OLED_DEMO
static void OLED_ErrorBlink(uint32_t delay_ms)
{
    while (1) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(delay_ms);
    }
}

static HAL_StatusTypeDef I2C1_InitPins(uint16_t scl_pin, uint16_t sda_pin, const char *label)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9);

    GPIO_InitTypeDef g = {0};
    g.Pin       = scl_pin | sda_pin;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &g);

    if (hi2c1.Instance != NULL) {
        HAL_I2C_DeInit(&hi2c1);
    }
    hi2c1.Instance              = I2C1;
    hi2c1.Init.Timing           = 0x10909CEC; /* 400 kHz @ 80 MHz PCLK1 */
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        printf("[OLED] I2C1 init failed on %s\r\n", label ? label : "pins");
        return HAL_ERROR;
    }

    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        printf("[OLED] I2C1 analog filter config failed\r\n");
        return HAL_ERROR;
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK) {
        printf("[OLED] I2C1 digital filter config failed\r\n");
        return HAL_ERROR;
    }

    if (label) {
        s_oled_pinset_label = label;
    }
    printf("[OLED] I2C1 configured on %s\r\n", s_oled_pinset_label);
    return HAL_OK;
}

static uint8_t OLED_ProbeAddress(uint8_t *addr_out)
{
    HAL_StatusTypeDef st;
    uint32_t error;
    uint8_t addr = OLED_I2C_ADDR_8BIT;

    if (addr_out == NULL) return 0;

    st = HAL_I2C_IsDeviceReady(&hi2c1, addr, 3, 20);
    error = HAL_I2C_GetError(&hi2c1);
    printf("[OLED] probe 0x%02X: %d (err=0x%08lX)\r\n",
           addr >> 1, (int)st, (unsigned long)error);
    if (st == HAL_OK) {
        *addr_out = addr;
        return 1;
    }

    addr = (0x3D << 1);
    st = HAL_I2C_IsDeviceReady(&hi2c1, addr, 3, 20);
    error = HAL_I2C_GetError(&hi2c1);
    printf("[OLED] probe 0x%02X: %d (err=0x%08lX)\r\n",
           addr >> 1, (int)st, (unsigned long)error);
    if (st == HAL_OK) {
        *addr_out = addr;
        return 1;
    }

    return 0;
}

static void OLED_DemoLoop(void)
{
    uint8_t addr = 0;

    if (I2C1_InitPins(GPIO_PIN_8, GPIO_PIN_9, "I2C1 PB8/PB9") == HAL_OK) {
        (void)OLED_ProbeAddress(&addr);
    }
    if (addr == 0) {
        if (I2C1_InitPins(GPIO_PIN_6, GPIO_PIN_7, "I2C1 PB6/PB7") == HAL_OK) {
            (void)OLED_ProbeAddress(&addr);
        }
    }
    if (addr == 0) {
        printf("[OLED] no ACK at 0x3C/0x3D on either pinset\r\n");
        OLED_ErrorBlink(700);
    }

    oled_config_t cfg = {
        .hi2c    = &hi2c1,
        .address = addr,
    };

    if (oled_init(&cfg) != OLED_OK) {
        OLED_ErrorBlink(250);
    }

    oled_set_contrast(0xCF);
    oled_framebuffer_clear();
    oled_draw_string(0, 0, "OLED OK", OLED_COLOR_WHITE);
    oled_draw_string(0, 16, s_oled_pinset_label, OLED_COLOR_WHITE);
    oled_framebuffer_update_display();

    while (1) {
        HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
        HAL_Delay(500);
    }
}
#endif

/* ========================================================================== */
/* Required interrupt handlers                                                */
/* ========================================================================== */
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
        /* Park here on fatal error. Attach debugger to inspect. */
    }
}
