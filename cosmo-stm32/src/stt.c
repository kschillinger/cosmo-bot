/**
 * stt.c — STUB. See stt.h.
 *
 * "Inference" completes ~300 ms after stt_process() and returns a hardcoded
 * recognized phrase so the rest of the pipeline has something to work with.
 */

#include "stt.h"
#include "system_utils.h"

#include <stdio.h>

#define STUB_STT_DURATION_MS  300U

static uint8_t  s_inference_active  = 0;
static uint32_t s_inference_start_ms = 0;
static char     s_result[]          = "hello cosmo";

void stt_init(void)
{
    printf("[STT] stt_init() (stub)\r\n");
}

void stt_process(uint8_t *audio_buffer, uint16_t buffer_size)
{
    (void)audio_buffer;
    printf("[STT] stt_process(size=%u) (stub)\r\n", (unsigned)buffer_size);
    s_inference_active   = 1;
    s_inference_start_ms = system_get_tick_ms();
}

uint8_t stt_is_complete(void)
{
    if (!s_inference_active) return 0;
    if ((system_get_tick_ms() - s_inference_start_ms) >= STUB_STT_DURATION_MS) {
        s_inference_active = 0;
        return 1;
    }
    return 0;
}

char *stt_get_result(void)
{
    printf("[STT] stt_get_result() -> \"%s\" (stub)\r\n", s_result);
    return s_result;
}
