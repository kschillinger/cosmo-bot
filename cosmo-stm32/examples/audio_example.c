/*
 * audio_example.c — bring-up demo for the audio playback driver.
 *
 * Generates a 1-second 1 kHz sine wave at 16 kHz / 16-bit PCM, then
 * plays it through DAC1 → PAM8302 → speaker. Cycles through volume
 * steps so you can ear-test the gain pipeline.
 *
 * To run as a standalone PlatformIO env, add to platformio.ini:
 *
 *   [env:nucleo_l476rg_audio_demo]
 *   extends = env:nucleo_l476rg
 *   build_src_filter = +<*> +<../examples/audio_example.c>
 *   build_flags = -DAUDIO_DEMO_MAIN
 */

#ifdef AUDIO_DEMO_MAIN

#include "stm32l4xx_hal.h"
#include "audio_playback.h"
#include <math.h>

/* 1 second of 1 kHz sine at 16 kHz sample rate. */
#define DEMO_SAMPLES   16000U
#define DEMO_TONE_HZ   1000.0f

static int16_t s_tone[DEMO_SAMPLES];

/* Forward decls for the standard CubeMX-style init plumbing. */
void SystemClock_Config(void);
static void demo_generate_tone(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    audio_init();
    demo_generate_tone();

    const uint8_t volumes[] = { 25U, 50U, 75U, 100U };

    for (;;) {
        for (uint32_t v = 0; v < sizeof(volumes); ++v) {
            audio_set_volume(volumes[v]);
            audio_play_buffer(s_tone, DEMO_SAMPLES);

            /* Spin until the clip finishes. In the real FSM you'd be
             * doing other work here (driving the display, polling UART,
             * etc.) — playback is fully non-blocking once started.       */
            while (!audio_playback_is_done()) {
                HAL_Delay(10);
            }

            HAL_Delay(500);  /* gap between volume steps */
        }
    }
}

static void demo_generate_tone(void)
{
    const float two_pi_f_over_fs =
        2.0f * 3.14159265358979f * DEMO_TONE_HZ / (float)AUDIO_SAMPLE_RATE_HZ;

    /* 80 % of full scale — leaves headroom so software gain == 1.0
     * doesn't clip. Real TTS audio is usually quieter than this.        */
    const float amp = 0.8f * 32767.0f;

    for (uint32_t i = 0; i < DEMO_SAMPLES; ++i) {
        float v = amp * sinf(two_pi_f_over_fs * (float)i);
        s_tone[i] = (int16_t)v;
    }
}

/* ----------------------------------------------------------------------- */
/* Standard 80 MHz clock config for the L476 — same one main.c uses on the  */
/* phase-1 UART build. Keep in sync if that one ever changes.               */
/* ----------------------------------------------------------------------- */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType  = RCC_OSCILLATORTYPE_MSI;
    osc.MSIState        = RCC_MSI_ON;
    osc.MSIClockRange   = RCC_MSIRANGE_6;       /* 4 MHz */
    osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    osc.PLL.PLLState    = RCC_PLL_ON;
    osc.PLL.PLLSource   = RCC_PLLSOURCE_MSI;
    osc.PLL.PLLM        = 1;
    osc.PLL.PLLN        = 40;                   /* 4 MHz × 40 / 2 = 80 MHz */
    osc.PLL.PLLP        = RCC_PLLP_DIV7;
    osc.PLL.PLLQ        = RCC_PLLQ_DIV2;
    osc.PLL.PLLR        = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) { while (1) {} }

    clk.ClockType      = RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) { while (1) {} }
}

#endif /* AUDIO_DEMO_MAIN */
