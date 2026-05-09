/*
 * oled_example.c — standalone bring-up demo for the OLED driver.
 *
 * NOT compiled by the default cosmo-stm32 PlatformIO build; copy into main.c
 * (or add a separate [env:nucleo_l476rg_oled_demo] env in platformio.ini that
 * sets `build_src_filter = +<*.c> -<main.c> +<../examples/oled_example/oled_example.c>`)
 * to run it.
 *
 * Demo cycles through the four runtime display states with realistic timing:
 *   IDLE       → 3 s
 *   LISTENING  → 3 s (synthetic sine waveform)
 *   PROCESSING → 3 s
 *   RESPONDING → 5 s
 * ...then loops.
 *
 * Pin map (matches the wiring diagram in display/README.md):
 *   SPI1_SCK   PA5
 *   SPI1_MOSI  PA7
 *   OLED_CS    PA4   (active LOW)
 *   OLED_DC    PB0   (HIGH=data, LOW=cmd)
 *   OLED_RST   PB1   (active LOW)
 */

#include "stm32l4xx_hal.h"
#include "display/oled_display.h"

#include <math.h>

/* ========================================================================== */
/*  CubeMX-style handles (declare here for a self-contained example).         */
/* ========================================================================== */

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);

/* ========================================================================== */
/*  Synthetic waveform for the listening demo                                 */
/* ========================================================================== */

#define DEMO_SAMPLES 128
static uint16_t demo_audio[DEMO_SAMPLES];

static void fill_demo_waveform(uint16_t phase)
{
    for (uint16_t i = 0; i < DEMO_SAMPLES; ++i) {
        /* Two-tone sine, slowly modulated in amplitude.                  */
        float t = (float)(i + phase) * 0.10f;
        float amp = 0.4f + 0.4f * sinf((float)phase * 0.05f);
        float v = amp * (sinf(t) + 0.5f * sinf(2.7f * t));
        int32_t s = (int32_t)(32768.0f + v * 18000.0f);
        if (s < 0)      s = 0;
        if (s > 65535)  s = 65535;
        demo_audio[i] = (uint16_t)s;
    }
}

/* ========================================================================== */
/*  Main                                                                      */
/* ========================================================================== */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();

    oled_config_t cfg = {
        .hspi      = &hspi1,
        .cs_port   = GPIOA, .cs_pin  = GPIO_PIN_4,
        .dc_port   = GPIOB, .dc_pin  = GPIO_PIN_0,
        .rst_port  = GPIOB, .rst_pin = GPIO_PIN_1
    };

    if (oled_init(&cfg) != OLED_OK) {
        /* Fall back to a slow heartbeat on the user LED so we know we're alive. */
        while (1) { HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5); HAL_Delay(200); }
    }

    oled_set_contrast(0xCF);

    uint16_t frame = 0;
    for (;;) {
        /* IDLE — 3 s @ 10 Hz = 30 frames                                  */
        oled_display_set_state(DISPLAY_STATE_IDLE);
        for (int i = 0; i < 30; ++i) {
            oled_update_animation_frame(frame++);
            oled_framebuffer_update_display();
            HAL_Delay(100);
        }

        /* LISTENING — 3 s, refresh waveform every frame                   */
        oled_display_set_state(DISPLAY_STATE_LISTENING);
        for (int i = 0; i < 30; ++i) {
            fill_demo_waveform(frame);
            oled_display_listening(demo_audio, DEMO_SAMPLES);
            oled_framebuffer_update_display();
            HAL_Delay(100);
            ++frame;
        }

        /* PROCESSING — 3 s                                                */
        oled_display_set_state(DISPLAY_STATE_PROCESSING);
        for (int i = 0; i < 30; ++i) {
            oled_update_animation_frame(frame++);
            oled_framebuffer_update_display();
            HAL_Delay(100);
        }

        /* RESPONDING — 5 s, with a multi-line response                    */
        oled_display_set_state(DISPLAY_STATE_RESPONDING);
        for (int i = 0; i < 50; ++i) {
            oled_display_responding(
                "Sure! The capital of France is Paris. Anything else?",
                frame++);
            oled_framebuffer_update_display();
            HAL_Delay(100);
        }
    }
}

/* ========================================================================== */
/*  HAL glue (minimal, would normally be CubeMX-generated)                    */
/* ========================================================================== */

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) oled_spi_tx_complete_isr();
}

static void SystemClock_Config(void)
{
    /* HSI16 + PLL → 80 MHz SYSCLK. Identical to the cosmo-stm32 main.c.   */
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState            = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM            = 1;
    osc.PLL.PLLN            = 10;
    osc.PLL.PLLP            = RCC_PLLP_DIV7;
    osc.PLL.PLLQ            = RCC_PLLQ_DIV2;
    osc.PLL.PLLR            = RCC_PLLR_DIV2;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4);
}

static void MX_GPIO_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    /* CS / DC / RST as push-pull outputs, idle high (CS,RST), low (DC).  */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);   /* CS */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); /* DC */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);   /* RST */

    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;

    g.Pin = GPIO_PIN_4;          HAL_GPIO_Init(GPIOA, &g);
    g.Pin = GPIO_PIN_0|GPIO_PIN_1; HAL_GPIO_Init(GPIOB, &g);

    /* Onboard LD2 for the heartbeat fallback path. */
    g.Pin = GPIO_PIN_5; HAL_GPIO_Init(GPIOA, &g);
}

static void MX_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
}

static void MX_SPI1_Init(void)
{
    /* Enable peripheral clocks before HAL_SPI_Init / HAL_DMA_Init.        */
    __HAL_RCC_SPI1_CLK_ENABLE();

    /* SPI1 SCK/MOSI on PA5/PA7 in AF5.                                    */
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_5 | GPIO_PIN_7;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_LOW;     /* mode 0 */
    hspi1.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;  /* 80/8 = 10 MHz */
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 7;
    HAL_SPI_Init(&hspi1);

    /* DMA1 Channel 3 → SPI1_TX. */
    hdma_spi1_tx.Instance                 = DMA1_Channel3;
    hdma_spi1_tx.Init.Request             = DMA_REQUEST_1;
    hdma_spi1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi1_tx.Init.Mode                = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority            = DMA_PRIORITY_MEDIUM;
    HAL_DMA_Init(&hdma_spi1_tx);
    __HAL_LINKDMA(&hspi1, hdmatx, hdma_spi1_tx);
}

/* DMA channel ISR — must call HAL_DMA_IRQHandler so the SPI-level
 * complete callback fires.                                                */
void DMA1_Channel3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_spi1_tx);
}
