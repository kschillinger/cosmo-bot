/*
 * oled_animations.c — Layer 5 implementation
 *
 * State-driven display routines for the cosmo-bot dialogue loop.
 * Faces are drawn from primitives (no bitmap assets) so they parameterize
 * cleanly with the animation frame counter.
 *
 *  Idle        — soft sleeping face, slow blink every ~2 s
 *  Listening   — vertical-bar audio waveform across the bottom half
 *  Processing  — orbiting dot ring + "Thinking..."
 *  Responding  — happy face + word-wrapped response text
 *  Error       — frowning face + error message
 *
 * All routines write into the framebuffer; the caller invokes
 * oled_framebuffer_update_display() to push.
 */

#include "oled_display.h"
#include <string.h>

/* ========================================================================== */
/*  State                                                                     */
/* ========================================================================== */

static oled_display_state_t  s_state           = DISPLAY_STATE_IDLE;
static const char           *s_response_text   = "";
static const char           *s_error_text      = "";
static const uint16_t       *s_audio_samples   = 0;
static uint16_t              s_audio_count     = 0;

void oled_display_set_state(oled_display_state_t state)
{
    s_state = state;
    /* Caller drives the animation by calling oled_update_animation_frame().
     * Do an immediate frame-0 render so the new state is visible right away
     * if the caller forgets to call update before the next frame tick.    */
    oled_update_animation_frame(0);
}

oled_display_state_t oled_display_get_state(void) { return s_state; }

/* ========================================================================== */
/*  Face primitives                                                           */
/* ========================================================================== */

/* Open eye: filled circle. Closed eye: short horizontal line.              */
static void draw_eye(int16_t cx, int16_t cy, uint8_t open)
{
    if (open) {
        oled_draw_circle(cx, cy, 6, 1, OLED_COLOR_WHITE);
        /* small black highlight to feel alive */
        oled_set_pixel((int16_t)(cx - 2), (int16_t)(cy - 2), OLED_COLOR_BLACK);
        oled_set_pixel((int16_t)(cx - 1), (int16_t)(cy - 2), OLED_COLOR_BLACK);
    } else {
        oled_draw_hline((int16_t)(cx - 5), cy, 11, OLED_COLOR_WHITE);
    }
}

/* Smile: lower half of a circle. We use the midpoint algorithm and only
 * keep points where dy >= 0 (lower hemisphere relative to the mouth pivot).
 * Cheaper than rolling a custom arc: just call draw_circle and clip via
 * a temporary buffer? — at this resolution, drawing the full circle and
 * letting the upper half overlap with the face background is fine. To
 * keep the face clean we instead trace the arc directly.                  */
static void draw_smile(int16_t cx, int16_t cy, int16_t r)
{
    /* Bresenham circle, plot only lower half. */
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

/* Frown: upper half of a circle (mirrored smile).                         */
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

/* ========================================================================== */
/*  Idle                                                                      */
/* ========================================================================== */

void oled_display_idle(uint16_t frame)
{
    oled_framebuffer_clear();

    /* Blink every ~2 s. At a 10 Hz frame tick that's frame % 20 == 0..1.   */
    const uint8_t blinking = ((frame % 20u) < 2u);

    draw_eye(46, 50, !blinking);
    draw_eye(82, 50, !blinking);

    /* Gentle smile (small radius arc).                                    */
    draw_smile(64, 78, 8);

    /* Bottom label.                                                       */
    const char *msg = "Ready to listen";
    int16_t w = (int16_t)oled_measure_string(msg);
    oled_draw_string((int16_t)((OLED_WIDTH - w) / 2), 110, msg, OLED_COLOR_WHITE);
}

/* ========================================================================== */
/*  Listening                                                                 */
/* ========================================================================== */

/*  Top half: open, slightly larger eyes (looks attentive)
 *  Bottom half: vertical-bar waveform from recent audio amplitudes.       */
void oled_display_listening(const uint16_t *audio_samples, uint16_t sample_count)
{
    s_audio_samples = audio_samples;
    s_audio_count   = sample_count;

    oled_framebuffer_clear();

    /* Eyes — wide, alert. */
    draw_eye(40, 28, 1);
    draw_eye(88, 28, 1);

    /* Waveform area: y in [60..120], x full width.
     * Bar count = 32, each 4 px wide. We pick samples evenly across the
     * provided buffer.                                                    */
    const int16_t base_y    = 90;
    const int16_t max_amp   = 28;
    const int16_t bar_width = 4;
    const int16_t bar_count = OLED_WIDTH / bar_width;  /* 32 */

    if (audio_samples && sample_count > 0) {
        for (int16_t b = 0; b < bar_count; ++b) {
            const uint32_t idx = ((uint32_t)b * sample_count) / (uint32_t)bar_count;
            int32_t s = (int32_t)audio_samples[idx] - 32768;
            if (s < 0) s = -s;
            int32_t h = (s * max_amp) / 32768;
            if (h < 1)        h = 1;
            if (h > max_amp)  h = max_amp;

            const int16_t x = (int16_t)(b * bar_width);
            oled_draw_rect(x, (int16_t)(base_y - h), (int16_t)(bar_width - 1),
                           (int16_t)(2*h), 1, OLED_COLOR_WHITE);
        }
    } else {
        /* No audio yet: show a flat baseline. */
        oled_draw_hline(0, base_y, OLED_WIDTH, OLED_COLOR_WHITE);
    }

    const char *msg = "Listening...";
    int16_t w = (int16_t)oled_measure_string(msg);
    oled_draw_string((int16_t)((OLED_WIDTH - w) / 2), 0, msg, OLED_COLOR_WHITE);
}

/* ========================================================================== */
/*  Processing                                                                */
/* ========================================================================== */

/* 8-step orbiting dot ring around centerpoint.                            */
void oled_display_processing(uint16_t frame)
{
    oled_framebuffer_clear();

    const int16_t cx = 64, cy = 56, r = 22;
    /* Pre-computed unit vectors * 32, 8 directions starting at 12 o'clock,
     * CW. (cos, sin) of (0, 45, 90, ..., 315) deg, scaled.                */
    static const int8_t dirs[8][2] = {
        {  0, -32 }, { 23, -23 }, { 32,   0 }, { 23,  23 },
        {  0,  32 }, {-23,  23 }, {-32,   0 }, {-23, -23 }
    };

    /* Draw 8 dots; the "leading" one (at this frame's phase) is bigger. */
    const uint8_t lead = (uint8_t)(frame & 7);
    for (uint8_t i = 0; i < 8; ++i) {
        const int16_t dx = (int16_t)((int32_t)dirs[i][0] * r / 32);
        const int16_t dy = (int16_t)((int32_t)dirs[i][1] * r / 32);
        const int16_t px = (int16_t)(cx + dx);
        const int16_t py = (int16_t)(cy + dy);
        const int16_t dot_r = (i == lead) ? 3 : ((i == ((lead + 7) & 7)) ? 2 : 1);
        oled_draw_circle(px, py, dot_r, 1, OLED_COLOR_WHITE);
    }

    const char *msg = "Thinking...";
    int16_t w = (int16_t)oled_measure_string(msg);
    oled_draw_string((int16_t)((OLED_WIDTH - w) / 2), 100, msg, OLED_COLOR_WHITE);
}

/* ========================================================================== */
/*  Responding                                                                */
/* ========================================================================== */

void oled_display_responding(const char *response_text, uint16_t frame)
{
    s_response_text = response_text ? response_text : "";

    oled_framebuffer_clear();

    /* Smaller happy face, top of screen (rest reserved for text).        */
    const uint8_t blinking = ((frame % 30u) < 2u);
    draw_eye(50, 16, !blinking);
    draw_eye(78, 16, !blinking);
    draw_smile(64, 28, 5);

    /* Divider line. */
    oled_draw_hline(8, 44, OLED_WIDTH - 16, OLED_COLOR_WHITE);

    /* Word-wrapped response in the lower 80 px.                          */
    oled_draw_string_wrapped(2, 50, OLED_WIDTH - 4, s_response_text, OLED_COLOR_WHITE);
}

/* ========================================================================== */
/*  Error                                                                     */
/* ========================================================================== */

void oled_display_error(const char *error_message)
{
    s_error_text = error_message ? error_message : "Unknown error";

    oled_framebuffer_clear();

    /* Closed/sad eyes (tilted lines).                                    */
    oled_draw_line(36, 22, 50, 28, OLED_COLOR_WHITE);
    oled_draw_line(36, 23, 50, 29, OLED_COLOR_WHITE);
    oled_draw_line(78, 28, 92, 22, OLED_COLOR_WHITE);
    oled_draw_line(78, 29, 92, 23, OLED_COLOR_WHITE);

    /* Frown. */
    draw_frown(64, 50, 8);

    /* Error label + message. */
    const char *hdr = "Error";
    int16_t w = (int16_t)oled_measure_string(hdr);
    oled_draw_string((int16_t)((OLED_WIDTH - w) / 2), 70, hdr, OLED_COLOR_WHITE);
    oled_draw_string_wrapped(2, 84, OLED_WIDTH - 4, s_error_text, OLED_COLOR_WHITE);
}

/* ========================================================================== */
/*  Animation tick                                                            */
/* ========================================================================== */

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
