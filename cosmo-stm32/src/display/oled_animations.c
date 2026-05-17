/*
 * oled_animations.c — Layer 5, sized for 128x64.
 *
 * State-driven display routines for the cosmo-bot dialogue loop. Faces
 * are drawn from primitives so they parameterize cleanly with frame.
 *
 *  Idle        — soft sleeping face, slow blink every ~2 s
 *  Listening   — compact eyes + audio waveform across the bottom half
 *  Processing  — orbiting dot ring + "Thinking..."
 *  Responding  — small happy face on top strip + word-wrapped text
 *  Error       — X-eyes + frown + error message
 *
 * Coordinates assume 128x64. If you switch to a 128x128 panel via
 * OLED_PANEL_128x128_SH1107, the face will appear in the top half — bump
 * y coords or rewrite for the larger area as desired.
 */

#include "oled_display.h"
#include <string.h>

static oled_display_state_t  s_state           = DISPLAY_STATE_IDLE;
static const char           *s_response_text   = "";
static const char           *s_error_text      = "";
static const uint16_t       *s_audio_samples   = 0;
static uint16_t              s_audio_count     = 0;

void oled_display_set_state(oled_display_state_t state)
{
    s_state = state;
    oled_update_animation_frame(0);
}

oled_display_state_t oled_display_get_state(void) { return s_state; }

/* ---- Face primitives ----------------------------------------------------- */

static void draw_eye(int16_t cx, int16_t cy, uint8_t open, int16_t r)
{
    if (open) {
        oled_draw_circle(cx, cy, r, 1, OLED_COLOR_WHITE);
        if (r >= 3) {
            oled_set_pixel((int16_t)(cx - 1), (int16_t)(cy - 1), OLED_COLOR_BLACK);
        }
    } else {
        oled_draw_hline((int16_t)(cx - r), cy, (int16_t)(2*r + 1), OLED_COLOR_WHITE);
    }
}

/* Lower half of a circle (Bresenham). */
static void draw_smile(int16_t cx, int16_t cy, int16_t r)
{
    int16_t x = 0, y = r;
    int16_t d = (int16_t)(1 - r);
    while (x <= y) {
        oled_set_pixel((int16_t)(cx + x), (int16_t)(cy + y), OLED_COLOR_WHITE);
        oled_set_pixel((int16_t)(cx - x), (int16_t)(cy + y), OLED_COLOR_WHITE);
        oled_set_pixel((int16_t)(cx + y), (int16_t)(cy + x), OLED_COLOR_WHITE);
        oled_set_pixel((int16_t)(cx - y), (int16_t)(cy + x), OLED_COLOR_WHITE);
        if (d < 0) d = (int16_t)(d + 2*x + 3);
        else       { d = (int16_t)(d + 2*(x - y) + 5); --y; }
        ++x;
    }
}

/* Upper half of a circle. */
static void draw_frown(int16_t cx, int16_t cy, int16_t r)
{
    int16_t x = 0, y = r;
    int16_t d = (int16_t)(1 - r);
    while (x <= y) {
        oled_set_pixel((int16_t)(cx + x), (int16_t)(cy - y), OLED_COLOR_WHITE);
        oled_set_pixel((int16_t)(cx - x), (int16_t)(cy - y), OLED_COLOR_WHITE);
        oled_set_pixel((int16_t)(cx + y), (int16_t)(cy - x), OLED_COLOR_WHITE);
        oled_set_pixel((int16_t)(cx - y), (int16_t)(cy - x), OLED_COLOR_WHITE);
        if (d < 0) d = (int16_t)(d + 2*x + 3);
        else       { d = (int16_t)(d + 2*(x - y) + 5); --y; }
        ++x;
    }
}

/* ---- Idle ---------------------------------------------------------------- */

void oled_display_idle(uint16_t frame)
{
    oled_framebuffer_clear();
    const uint8_t blinking = ((frame % 20u) < 2u);

    draw_eye(44, 22, !blinking, 4);
    draw_eye(84, 22, !blinking, 4);
    draw_smile(64, 38, 7);

    const char *msg = "Ready to listen";
    int16_t w = (int16_t)oled_measure_string(msg);
    oled_draw_string((int16_t)((OLED_WIDTH - w) / 2),
                     (int16_t)(OLED_HEIGHT - OLED_FONT_H),
                     msg, OLED_COLOR_WHITE);
}

/* ---- Listening ----------------------------------------------------------- */

void oled_display_listening(const uint16_t *audio_samples, uint16_t sample_count)
{
    s_audio_samples = audio_samples;
    s_audio_count   = sample_count;

    oled_framebuffer_clear();

    const char *msg = "Listening...";
    int16_t w = (int16_t)oled_measure_string(msg);
    oled_draw_string((int16_t)((OLED_WIDTH - w) / 2), 0, msg, OLED_COLOR_WHITE);

    draw_eye(40, 18, 1, 3);
    draw_eye(88, 18, 1, 3);

    const int16_t base_y    = 46;
    const int16_t max_amp   = 14;
    const int16_t bar_width = 4;
    const int16_t bar_count = OLED_WIDTH / bar_width;   /* 32 */

    if (audio_samples && sample_count > 0) {
        for (int16_t b = 0; b < bar_count; ++b) {
            const uint32_t idx = ((uint32_t)b * sample_count) / (uint32_t)bar_count;
            int32_t v = (int32_t)audio_samples[idx] - 32768;
            if (v < 0) v = -v;
            int32_t h = (v * max_amp) / 32768;
            if (h < 1)       h = 1;
            if (h > max_amp) h = max_amp;

            const int16_t x = (int16_t)(b * bar_width);
            oled_draw_rect(x, (int16_t)(base_y - h),
                           (int16_t)(bar_width - 1),
                           (int16_t)(2*h), 1, OLED_COLOR_WHITE);
        }
    } else {
        oled_draw_hline(0, base_y, OLED_WIDTH, OLED_COLOR_WHITE);
    }
}

/* ---- Processing ---------------------------------------------------------- */

void oled_display_processing(uint16_t frame)
{
    oled_framebuffer_clear();

    const int16_t cx = 64, cy = 26, r = 14;
    static const int8_t dirs[8][2] = {
        {  0, -32 }, { 23, -23 }, { 32,   0 }, { 23,  23 },
        {  0,  32 }, {-23,  23 }, {-32,   0 }, {-23, -23 }
    };

    const uint8_t lead = (uint8_t)(frame & 7);
    for (uint8_t i = 0; i < 8; ++i) {
        const int16_t dx = (int16_t)((int32_t)dirs[i][0] * r / 32);
        const int16_t dy = (int16_t)((int32_t)dirs[i][1] * r / 32);
        const int16_t px = (int16_t)(cx + dx);
        const int16_t py = (int16_t)(cy + dy);
        const int16_t dot_r = (i == lead) ? 2 : ((i == ((lead + 7) & 7)) ? 1 : 0);
        if (dot_r == 0)  oled_set_pixel(px, py, OLED_COLOR_WHITE);
        else             oled_draw_circle(px, py, dot_r, 1, OLED_COLOR_WHITE);
    }

    const char *msg = "Thinking...";
    int16_t w = (int16_t)oled_measure_string(msg);
    oled_draw_string((int16_t)((OLED_WIDTH - w) / 2),
                     (int16_t)(OLED_HEIGHT - OLED_FONT_H),
                     msg, OLED_COLOR_WHITE);
}

/* ---- Responding ---------------------------------------------------------- */

void oled_display_responding(const char *response_text, uint16_t frame)
{
    s_response_text = response_text ? response_text : "";

    oled_framebuffer_clear();

    /* Small face on the top strip; rest is text. */
    const uint8_t blinking = ((frame % 30u) < 2u);
    draw_eye(54, 5, !blinking, 2);
    draw_eye(74, 5, !blinking, 2);
    draw_smile(64, 11, 3);

    oled_draw_hline(8, 18, OLED_WIDTH - 16, OLED_COLOR_WHITE);

    /* Wrapped text in lower 44 px (≈ 5 lines).                            */
    oled_draw_string_wrapped(2, 22, OLED_WIDTH - 4, s_response_text, OLED_COLOR_WHITE);
}

/* ---- Error --------------------------------------------------------------- */

void oled_display_error(const char *error_message)
{
    s_error_text = error_message ? error_message : "Unknown error";

    oled_framebuffer_clear();

    /* X-shaped eyes. */
    oled_draw_line(36, 6, 46, 14, OLED_COLOR_WHITE);
    oled_draw_line(46, 6, 36, 14, OLED_COLOR_WHITE);
    oled_draw_line(82, 6, 92, 14, OLED_COLOR_WHITE);
    oled_draw_line(92, 6, 82, 14, OLED_COLOR_WHITE);

    draw_frown(64, 26, 5);

    const char *hdr = "Error";
    int16_t w = (int16_t)oled_measure_string(hdr);
    oled_draw_string((int16_t)((OLED_WIDTH - w) / 2), 34, hdr, OLED_COLOR_WHITE);
    oled_draw_string_wrapped(2, 46, OLED_WIDTH - 4, s_error_text, OLED_COLOR_WHITE);
}

/* ---- Animation tick ------------------------------------------------------ */

void oled_update_animation_frame(uint16_t frame)
{
    switch (s_state) {
    case DISPLAY_STATE_IDLE:       oled_display_idle(frame);                                  break;
    case DISPLAY_STATE_LISTENING:  oled_display_listening(s_audio_samples, s_audio_count);    break;
    case DISPLAY_STATE_PROCESSING: oled_display_processing(frame);                            break;
    case DISPLAY_STATE_RESPONDING: oled_display_responding(s_response_text, frame);           break;
    case DISPLAY_STATE_ERROR:      oled_display_error(s_error_text);                          break;
    default: break;
    }
}
