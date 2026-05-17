/**
 * oled_display.c — STUB. See oled_display.h.
 *
 * Display calls happen on every fsm_run() iteration (~10 ms cadence), so
 * naive printf() inside each one would flood USART2. Each handler logs
 * once at state-entry-equivalent boundaries (frame == 0) and on
 * meaningful changes only.
 */

#include "oled_display.h"

#include <stdio.h>

void oled_init(void)
{
    printf("[OLED] oled_init() (stub)\r\n");
}

void oled_display_idle(void)
{
    static uint8_t logged = 0;
    if (!logged) {
        printf("[OLED] display_idle (stub)\r\n");
        logged = 1;
    }
    /* Subsequent calls in IDLE: silent. (logged resets when other states
     * call their own handlers — see helper below if extending.) */
}

void oled_display_listening(uint16_t *samples, uint16_t sample_count)
{
    (void)samples;
    static uint16_t last_count = 0;
    if (sample_count != last_count) {
        printf("[OLED] display_listening(samples=%u) (stub)\r\n",
               (unsigned)sample_count);
        last_count = sample_count;
    }
}

void oled_display_processing(uint16_t frame)
{
    /* Log every 10th frame to show animation progress without flooding. */
    if ((frame % 10U) == 0U) {
        printf("[OLED] display_processing(frame=%u) (stub)\r\n",
               (unsigned)frame);
    }
}

void oled_display_responding(const char *text, uint16_t frame)
{
    if (frame == 0) {
        printf("[OLED] display_responding(\"%s\") (stub)\r\n",
               text ? text : "(null)");
    }
}

void oled_display_error(const char *error_message)
{
    printf("[OLED] display_error(\"%s\") (stub)\r\n",
           error_message ? error_message : "(null)");
}

void oled_update_display(void)
{
    /* No-op for stub; real driver would push the framebuffer over SPI. */
}
