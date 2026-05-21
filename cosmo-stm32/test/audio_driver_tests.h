/**
 * audio_driver_tests.h — Audio driver test suite header
 *
 * Exported functions for integration into FSM test harness.
 */

#ifndef AUDIO_DRIVER_TESTS_H
#define AUDIO_DRIVER_TESTS_H

/**
 * run_all_audio_tests() — Execute full audio driver test suite
 *
 * Runs all phases (0–7) with UART output. Test results are printed
 * to UART, suitable for CI/CD parsing or manual review.
 *
 * Should be called once at startup or from a dedicated test command.
 */
void run_all_audio_tests(void);

#endif /* AUDIO_DRIVER_TESTS_H */
