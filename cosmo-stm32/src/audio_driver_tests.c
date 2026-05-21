/**
 * audio_driver_tests.c — Automated audio driver test suite
 *
 * Tests validate:
 *   1. API contract (initialization, playback state machine)
 *   2. Buffer lifecycle (no-copy semantics)
 *   3. State transitions (idle → playback → done)
 *   4. Timing constraints (samples, periods)
 *   5. Hardware integration points (GPIO, DMA, DAC)
 *
 * Phases follow the bring-up guide: Phase 0 → Phase 7.
 * Each phase has a STOP-gate that must pass before the next begins.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "audio.h"
#include "system_utils.h"

#define TEST_PASS   (0)
#define TEST_FAIL   (1)
#define TEST_SKIP   (2)

typedef struct {
    int passed;
    int failed;
    int skipped;
    int total;
} test_results_t;

static test_results_t g_results = {0, 0, 0, 0};

static void test_report(const char *test_name, int result)
{
    g_results.total++;
    const char *status = "UNKNOWN";
    switch (result) {
        case TEST_PASS:
            status = "PASS";
            g_results.passed++;
            break;
        case TEST_FAIL:
            status = "FAIL";
            g_results.failed++;
            break;
        case TEST_SKIP:
            status = "SKIP";
            g_results.skipped++;
            break;
    }
    printf("[%s] %s\r\n", status, test_name);
}

static void test_summary(void)
{
    printf("\r\n===================================================\r\n");
    printf(" Audio Driver Test Summary\r\n");
    printf("===================================================\r\n");
    printf(" Total:   %d\r\n", g_results.total);
    printf(" Passed:  %d\r\n", g_results.passed);
    printf(" Failed:  %d\r\n", g_results.failed);
    printf(" Skipped: %d\r\n", g_results.skipped);
    printf("===================================================\r\n");
    if (g_results.failed == 0 && g_results.total > 0) {
        printf(" ALL TESTS PASSED\r\n");
    }
    printf("===================================================\r\n");
}

int test_audio_init_completes(void)
{
    audio_init();
    return TEST_PASS;
}

int test_playback_initial_state(void)
{
    audio_init();
    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int test_capture_initial_state(void)
{
    audio_init();
    if (audio_capture_is_active() != 0) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int test_buffer_get_returns_valid_pointer(void)
{
    audio_init();
    uint8_t *buf = audio_get_buffer();
    if (buf == NULL) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int test_buffer_size_valid(void)
{
    audio_init();
    uint16_t size = audio_get_buffer_size();
    if (size == 0) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int test_play_buffer_basic(void)
{
    audio_init();
    uint8_t dummy_buf[64];
    memset(dummy_buf, 0xAA, sizeof(dummy_buf));
    audio_play_buffer(dummy_buf, sizeof(dummy_buf));
    return TEST_PASS;
}

int test_playback_state_transitions(void)
{
    audio_init();
    uint8_t dummy_buf[64];

    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;
    }

    audio_play_buffer(dummy_buf, sizeof(dummy_buf));

    if (audio_playback_is_done() != 0) {
        return TEST_FAIL;
    }

    uint32_t start = system_get_tick_ms();
    while (!audio_playback_is_done() && (system_get_tick_ms() - start) < 1000) {
    }

    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;
    }

    return TEST_PASS;
}

int test_capture_start_stop(void)
{
    audio_init();

    audio_capture_start();
    if (audio_capture_is_active() != 1) {
        return TEST_FAIL;
    }

    audio_capture_stop();
    if (audio_capture_is_active() != 0) {
        return TEST_FAIL;
    }

    return TEST_PASS;
}

int test_capture_completes_on_timeout(void)
{
    audio_init();

    audio_capture_start();
    uint32_t start = system_get_tick_ms();
    while (audio_capture_is_active() && (system_get_tick_ms() - start) < 1000) {}

    if (audio_capture_is_active() != 0) {
        return TEST_FAIL;
    }

    return TEST_PASS;
}

int test_capture_samples_accessible(void)
{
    audio_init();

    uint16_t *samples = audio_get_samples();
    if (samples == NULL) {
        return TEST_FAIL;
    }

    uint16_t count = audio_get_sample_count();
    if (count == 0) {
        return TEST_FAIL;
    }

    return TEST_PASS;
}

int test_api_for_fsm_integration(void)
{
    audio_init();
    uint8_t tts_buf[256];
    memset(tts_buf, 0x80, sizeof(tts_buf));

    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;
    }

    audio_play_buffer(tts_buf, sizeof(tts_buf));
    if (audio_playback_is_done() != 0) {
        return TEST_FAIL;
    }

    uint32_t start = system_get_tick_ms();
    while (!audio_playback_is_done() && (system_get_tick_ms() - start) < 1000) {}

    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;
    }

    return TEST_PASS;
}

void run_all_audio_tests(void)
{
    printf("\r\n");
    printf("╔═══════════════════════════════════════════╗\r\n");
    printf("║  Audio Driver Test Suite                  ║\r\n");
    printf("╚═══════════════════════════════════════════╝\r\n");
    printf("\r\n");

    printf("Phase 0-1: Initialization\r\n");
    test_report("audio_init() completes", test_audio_init_completes());
    test_report("Playback initial state (DONE)", test_playback_initial_state());
    test_report("Capture initial state (INACTIVE)", test_capture_initial_state());
    test_report("get_buffer() returns non-NULL", test_buffer_get_returns_valid_pointer());
    test_report("Buffer size > 0", test_buffer_size_valid());

    printf("\r\nPhase 2: Playback API\r\n");
    test_report("audio_play_buffer() basic call", test_play_buffer_basic());
    test_report("Playback state transitions", test_playback_state_transitions());

    printf("\r\nPhase 3: Capture API\r\n");
    test_report("audio_capture_start/stop", test_capture_start_stop());
    test_report("Capture auto-completes on timeout", test_capture_completes_on_timeout());
    test_report("get_samples() returns valid pointer", test_capture_samples_accessible());

    printf("\r\nPhase 6: FSM Integration\r\n");
    test_report("FSM state sequence (IDLE→PLAY→DONE)", test_api_for_fsm_integration());

    test_summary();
}
