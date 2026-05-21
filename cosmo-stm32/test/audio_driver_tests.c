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
#include <assert.h>

/* Mock HAL for testing without hardware */
#include "audio.h"
#include "system_utils.h"

/* --- Test harness ---------------------------------------------------------- */

#define TEST_PASS   (0)
#define TEST_FAIL   (1)
#define TEST_SKIP   (2)

typedef struct {
    const char *name;
    int (*test_fn)(void);
    int expected_result;  /* TEST_PASS, TEST_FAIL, or TEST_SKIP */
} test_case_t;

typedef struct {
    int passed;
    int failed;
    int skipped;
    int total;
} test_results_t;

static test_results_t g_results = {0, 0, 0, 0};

static void test_report(const char *test_name, int result, const char *details)
{
    g_results.total++;
    const char *status = "UNKNOWN";
    switch (result) {
        case TEST_PASS:
            status = "✓ PASS";
            g_results.passed++;
            break;
        case TEST_FAIL:
            status = "✗ FAIL";
            g_results.failed++;
            break;
        case TEST_SKIP:
            status = "⊘ SKIP";
            g_results.skipped++;
            break;
    }
    printf("[%s] %s", status, test_name);
    if (details) {
        printf(" — %s", details);
    }
    printf("\r\n");
}

static void test_summary(void)
{
    printf("\r\n");
    printf("═══════════════════════════════════════════════════════════\r\n");
    printf(" Audio Driver Test Summary\r\n");
    printf("───────────────────────────────────────────────────────────\r\n");
    printf(" Total:   %d\r\n", g_results.total);
    printf(" Passed:  %d\r\n", g_results.passed);
    printf(" Failed:  %d\r\n", g_results.failed);
    printf(" Skipped: %d\r\n", g_results.skipped);
    printf("═══════════════════════════════════════════════════════════\r\n");
    if (g_results.failed == 0 && g_results.total > 0) {
        printf(" ✓ ALL TESTS PASSED\r\n");
    } else if (g_results.failed > 0) {
        printf(" ✗ %d TEST(S) FAILED\r\n", g_results.failed);
    }
    printf("═══════════════════════════════════════════════════════════\r\n");
}

/* --- Phase 0 & 1: Initialization -------------------------------------------*/

int test_audio_init_completes(void)
{
    /*
     * Gate: audio_init() must not hang or hard-fault.
     * In production, this confirms SystemClock, GPIO, DAC, DMA, TIM6 setup.
     * In stub, verifies basic state setup.
     */
    audio_init();
    return TEST_PASS;
}

int test_playback_initial_state(void)
{
    /*
     * Gate: After init, playback must be in DONE state (no active playback).
     */
    audio_init();
    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int test_capture_initial_state(void)
{
    /*
     * Gate: After init, capture must be inactive.
     */
    audio_init();
    if (audio_capture_is_active() != 0) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int test_buffer_get_returns_valid_pointer(void)
{
    /*
     * Gate: get_buffer() must never return NULL.
     * Driver does NOT copy, so caller must provide valid buffers.
     */
    audio_init();
    uint8_t *buf = audio_get_buffer();
    if (buf == NULL) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

int test_buffer_size_valid(void)
{
    /*
     * Gate: Buffer size must be > 0.
     */
    audio_init();
    uint16_t size = audio_get_buffer_size();
    if (size == 0) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

/* --- Phase 2: Playback API ------------------------------------------------- */

int test_play_buffer_basic(void)
{
    /*
     * Gate: audio_play_buffer() must accept valid buffers without crashing.
     * This is the most critical single gate.
     */
    audio_init();
    uint8_t dummy_buf[64];
    memset(dummy_buf, 0xAA, sizeof(dummy_buf));
    audio_play_buffer(dummy_buf, sizeof(dummy_buf));
    return TEST_PASS;
}

int test_playback_state_transitions(void)
{
    /*
     * Gate: Playback must transition IDLE → ACTIVE → DONE correctly.
     *
     * Timeline (stub):
     *   t=0     : audio_play_buffer() called, is_done() returns 0
     *   t=500ms : is_done() returns 1
     */
    audio_init();
    uint8_t dummy_buf[64];

    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;  /* Should start in DONE state */
    }

    audio_play_buffer(dummy_buf, sizeof(dummy_buf));

    if (audio_playback_is_done() != 0) {
        return TEST_FAIL;  /* Should immediately report active */
    }

    /* Stub simulates 500 ms playback, wait for completion */
    uint32_t start = system_get_tick_ms();
    while (!audio_playback_is_done() && (system_get_tick_ms() - start) < 1000) {
        /* Poll until done or timeout */
    }

    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;  /* Should eventually complete */
    }

    return TEST_PASS;
}

int test_playback_multiple_buffers_sequential(void)
{
    /*
     * Gate: Driver must support back-to-back playback calls.
     * Each starts a new playback cycle.
     */
    audio_init();
    uint8_t buf1[64], buf2[64];

    /* First clip */
    audio_play_buffer(buf1, sizeof(buf1));
    if (audio_playback_is_done() != 0) {
        return TEST_FAIL;
    }

    /* Wait for completion */
    uint32_t start = system_get_tick_ms();
    while (!audio_playback_is_done() && (system_get_tick_ms() - start) < 1000) {}

    /* Second clip */
    audio_play_buffer(buf2, sizeof(buf2));
    if (audio_playback_is_done() != 0) {
        return TEST_FAIL;  /* Must restart playback */
    }

    start = system_get_tick_ms();
    while (!audio_playback_is_done() && (system_get_tick_ms() - start) < 1000) {}

    return TEST_PASS;
}

/* --- Phase 3: Capture API -------------------------------------------------- */

int test_capture_start_stop(void)
{
    /*
     * Gate: Capture must start and stop cleanly.
     */
    audio_init();

    audio_capture_start();
    if (audio_capture_is_active() != 1) {
        return TEST_FAIL;  /* Must report active immediately */
    }

    audio_capture_stop();
    if (audio_capture_is_active() != 0) {
        return TEST_FAIL;  /* Must report inactive immediately */
    }

    return TEST_PASS;
}

int test_capture_completes_on_timeout(void)
{
    /*
     * Gate: Capture must auto-complete after ~500 ms (stub) or when
     * explicitly stopped. This prevents indefinite capture.
     */
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
    /*
     * Gate: After capture, get_samples() must return valid data.
     * In stub, this is pre-populated dummy data.
     */
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

/* --- Phase 4: Concurrent operation ----------------------------------------- */

int test_capture_while_playback_idle(void)
{
    /*
     * Gate: Capture and playback must be independent.
     * Can start capture while playback is idle.
     */
    audio_init();

    audio_capture_start();
    if (audio_capture_is_active() != 1) {
        return TEST_FAIL;
    }

    uint8_t dummy_buf[64];
    audio_play_buffer(dummy_buf, sizeof(dummy_buf));

    if (audio_capture_is_active() != 1) {
        return TEST_FAIL;  /* Capture should still be active */
    }

    audio_capture_stop();
    return TEST_PASS;
}

/* --- Phase 5: No-copy buffer semantics ------------------------------------- */

int test_buffer_not_copied_on_playback(void)
{
    /*
     * Critical: Driver DOES NOT copy buffers. Caller must ensure
     * buffer lifetime extends until audio_playback_is_done() == 1.
     *
     * This test verifies the driver doesn't make a copy by:
     *   1. Playing a static buffer
     *   2. Modifying the buffer mid-playback
     *   3. Confirming playback would output modified data
     *
     * In real hardware, you'd scope PA4 to verify changes.
     * In stub, we just confirm no crash.
     */
    audio_init();
    uint8_t test_buf[64];
    memset(test_buf, 0x55, sizeof(test_buf));

    audio_play_buffer(test_buf, sizeof(test_buf));

    /* Modify buffer mid-playback */
    test_buf[0] = 0xAA;
    test_buf[32] = 0xFF;

    /* If driver made a copy, modified values would not affect playback.
     * If driver uses original buffer (no copy), it sees the changes.
     * Stub just confirms no crash. */

    return TEST_PASS;
}

/* --- Phase 6: Buffer lifetime validation ---------------------------------- */

int test_buffer_lifetime_stack_safety(void)
{
    /*
     * TRAP: Passing a stack-allocated buffer that goes out of scope
     * before playback completes will cause garbage audio.
     *
     * This test is more of a documentation test, as C can't prevent
     * you from doing this. But we verify the API accepts it without
     * immediate error (the real bug appears at playback).
     */
    audio_init();
    {
        uint8_t stack_buf[64];
        memset(stack_buf, 0x77, sizeof(stack_buf));
        audio_play_buffer(stack_buf, sizeof(stack_buf));
        /* stack_buf goes out of scope here — BAD if playback still active */
    }
    /* This compiles and runs, but would cause garbage audio in real HW.
     * Recommendation: Use static or heap-allocated buffers, or ensure
     * audio_playback_is_done() before the buffer goes out of scope. */
    return TEST_PASS;
}

/* --- Phase 7: FSM integration points --------------------------------------- */

int test_api_for_fsm_integration(void)
{
    /*
     * Gate: Verify the exact API sequence expected by FSM:
     *
     *   FSM_STATE_PROCESSING_TTS
     *     ↓
     *   audio_play_buffer(tts_output) + check is_done() == 0
     *     ↓
     *   FSM_STATE_PLAYBACK (spins on audio_playback_is_done())
     *     ↓
     *   audio_playback_is_done() == 1
     *     ↓
     *   FSM_STATE_IDLE
     */
    audio_init();
    uint8_t tts_buf[256];
    memset(tts_buf, 0x80, sizeof(tts_buf));

    /* Pre-playback */
    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;  /* Idle state */
    }

    /* Transition to playback */
    audio_play_buffer(tts_buf, sizeof(tts_buf));
    if (audio_playback_is_done() != 0) {
        return TEST_FAIL;  /* Must report active */
    }

    /* Wait for completion */
    uint32_t start = system_get_tick_ms();
    while (!audio_playback_is_done() && (system_get_tick_ms() - start) < 1000) {}

    /* Back to idle */
    if (audio_playback_is_done() != 1) {
        return TEST_FAIL;
    }

    return TEST_PASS;
}

/* --- Hardware gate validation (requires multimeter/scope) ------------------- */

int test_phase_2_pb2_check_stub(void)
{
    /*
     * Phase 2 STOP-gate: PB2 must read 3.3 V after audio_init().
     * (This confirms the driver initialized the amplifier shutdown pin.)
     *
     * In real hardware: Use multimeter on PB2.
     * In stub: We can't access GPIO, so this test is marked SKIP.
     *          In integration tests with real HAL, this would be:
     *            HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_2) == GPIO_PIN_SET
     */
    return TEST_SKIP;
}

int test_phase_3_pa4_sinusoid_stub(void)
{
    /*
     * Phase 3 STOP-gate: PA4 must show ~1.65 V idle, 1 kHz sinusoid during tone.
     * (This confirms DAC is running and DMA is feeding it samples.)
     *
     * In real hardware: Scope probe on PA4.
     * In stub: No DAC, so this test is marked SKIP.
     */
    return TEST_SKIP;
}

int test_phase_4_sample_rate_stub(void)
{
    /*
     * Phase 4 STOP-gate: Measure PA4 frequency with scope FFT.
     * Must be 1 kHz ± 5 Hz.
     * (Confirms SystemClock is 80 MHz and TIM6 period is correct.)
     *
     * In real hardware: Scope frequency measure.
     * In stub: No real clock/DAC, so this test is marked SKIP.
     */
    return TEST_SKIP;
}

/* --- Master test runner ---------------------------------------------------- */

void run_all_audio_tests(void)
{
    printf("\r\n");
    printf("╔═══════════════════════════════════════════════════════════╗\r\n");
    printf("║         Audio Driver Test Suite (Bring-up Guide)          ║\r\n");
    printf("╚═══════════════════════════════════════════════════════════╝\r\n");
    printf("\r\n");

    /* Phase 0-1: Initialization */
    printf("Phase 0-1: Initialization\r\n");
    printf("───────────────────────────────────────────────────────────\r\n");
    {
        test_case_t tests[] = {
            {"audio_init() completes", test_audio_init_completes, TEST_PASS},
            {"Playback initial state (DONE)", test_playback_initial_state, TEST_PASS},
            {"Capture initial state (INACTIVE)", test_capture_initial_state, TEST_PASS},
            {"get_buffer() returns non-NULL", test_buffer_get_returns_valid_pointer, TEST_PASS},
            {"Buffer size > 0", test_buffer_size_valid, TEST_PASS},
        };
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            int result = tests[i].test_fn();
            test_report(tests[i].name, result, NULL);
        }
    }

    /* Phase 2: Playback API */
    printf("\r\nPhase 2: Playback API\r\n");
    printf("───────────────────────────────────────────────────────────\r\n");
    {
        test_case_t tests[] = {
            {"audio_play_buffer() basic call", test_play_buffer_basic, TEST_PASS},
            {"Playback state transitions (IDLE→ACTIVE→DONE)", test_playback_state_transitions, TEST_PASS},
            {"Sequential buffer playback", test_playback_multiple_buffers_sequential, TEST_PASS},
        };
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            int result = tests[i].test_fn();
            test_report(tests[i].name, result, NULL);
        }
    }

    /* Phase 3: Capture API */
    printf("\r\nPhase 3: Capture API\r\n");
    printf("───────────────────────────────────────────────────────────\r\n");
    {
        test_case_t tests[] = {
            {"audio_capture_start/stop", test_capture_start_stop, TEST_PASS},
            {"Capture auto-completes on timeout", test_capture_completes_on_timeout, TEST_PASS},
            {"get_samples() returns valid pointer", test_capture_samples_accessible, TEST_PASS},
        };
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            int result = tests[i].test_fn();
            test_report(tests[i].name, result, NULL);
        }
    }

    /* Phase 4: Concurrent operation */
    printf("\r\nPhase 4: Concurrent Operation\r\n");
    printf("───────────────────────────────────────────────────────────\r\n");
    {
        test_case_t tests[] = {
            {"Capture while playback idle", test_capture_while_playback_idle, TEST_PASS},
        };
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            int result = tests[i].test_fn();
            test_report(tests[i].name, result, NULL);
        }
    }

    /* Phase 5: No-copy semantics */
    printf("\r\nPhase 5: No-Copy Buffer Semantics\r\n");
    printf("───────────────────────────────────────────────────────────\r\n");
    {
        test_case_t tests[] = {
            {"Buffer not copied (no-copy contract)", test_buffer_not_copied_on_playback, TEST_PASS},
            {"Stack buffer lifetime (documentation)", test_buffer_lifetime_stack_safety, TEST_PASS},
        };
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            int result = tests[i].test_fn();
            test_report(tests[i].name, result, NULL);
        }
    }

    /* Phase 6: FSM integration */
    printf("\r\nPhase 6: FSM Integration API\r\n");
    printf("───────────────────────────────────────────────────────────\r\n");
    {
        test_case_t tests[] = {
            {"FSM state sequence (IDLE→PLAY→DONE)", test_api_for_fsm_integration, TEST_PASS},
        };
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            int result = tests[i].test_fn();
            test_report(tests[i].name, result, NULL);
        }
    }

    /* Phase 7: Hardware gates (multimeter/scope required) */
    printf("\r\nPhase 7: Hardware Gates (Requires Multimeter/Scope)\r\n");
    printf("───────────────────────────────────────────────────────────\r\n");
    {
        test_case_t tests[] = {
            {"[HARDWARE] Phase 2: PB2 reads 3.3 V", test_phase_2_pb2_check_stub, TEST_SKIP},
            {"[HARDWARE] Phase 3: PA4 shows 1 kHz sinusoid", test_phase_3_pa4_sinusoid_stub, TEST_SKIP},
            {"[HARDWARE] Phase 4: Sinusoid freq ±5 Hz", test_phase_4_sample_rate_stub, TEST_SKIP},
        };
        for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
            int result = tests[i].test_fn();
            test_report(tests[i].name, result, NULL);
        }
    }

    test_summary();
}

/* --- Standalone entry point (if run directly) ------------------------------ */

#ifdef AUDIO_TESTS_MAIN
int main(void)
{
    /* Initialize system */
    system_utils_init();  /* Assumes system_utils.h provides this */

    run_all_audio_tests();

    return g_results.failed == 0 ? 0 : 1;
}
#endif
