/**
 * dialogue.h — Cosmo Bot intent classifier + response picker
 * ============================================================================
 * Public API for the ESP32-C3-side conversational brain. Takes a user
 * utterance (raw text from the STT pipeline), routes it to one of the
 * intents defined in intents.h, and returns one of that intent's response
 * variants chosen pseudo-randomly with no-immediate-repeat.
 *
 * Pipeline
 * --------
 *   user_text ──► normalize ──► tokenize ──► score(each intent) ──► best
 *                                                                    │
 *                                                  random non-repeat ▼
 *                                                              response
 *
 * Scoring
 * -------
 * Each intent has a keyword list. The score is the percentage of the
 * intent's own keywords that appear in the input tokens (Jaccard-style
 * overlap from the intent's perspective). Highest score wins; ties are
 * broken by intent order in the enum (first-declared wins). If the top
 * score is below DIALOGUE_MIN_CONFIDENCE_PCT we fall through to
 * INTENT_FALLBACK so we never reply with "best guess" garbage.
 *
 * Statelessness
 * -------------
 * dialogue_respond() is a pure function of its input plus esp_random()'s
 * stream and the engine's saved "last response index per intent" state.
 * There is no conversation memory yet — that lives in a future module.
 *
 * Threading
 * ---------
 * The engine is single-threaded by design. The ESP32-C3 has one core; the
 * Arduino main loop is the only caller. If you ever call dialogue_respond()
 * from a FreeRTOS task and also from loop(), wrap the s_last_response_idx
 * updates in a mutex.
 * ============================================================================
 */
#ifndef COSMO_DIALOGUE_H
#define COSMO_DIALOGUE_H

#include "intents.h"

/* Threshold for "high enough confidence to use this intent". Set low
 * because keyword overlap is sparse — one keyword match out of three is a
 * solid hit (33%). Tune via testing:
 *   - Raise to suppress weak guesses (more INTENT_FALLBACK replies).
 *   - Lower to be more aggressive about classifying.
 *
 * Override by defining DIALOGUE_MIN_CONFIDENCE_PCT in build_flags before
 * this header is parsed.
 */
#ifndef DIALOGUE_MIN_CONFIDENCE_PCT
#define DIALOGUE_MIN_CONFIDENCE_PCT 25
#endif

struct DialogueResult {
    Intent      intent;       /* classified intent (FALLBACK if low conf)    */
    uint8_t     confidence;   /* 0-100; matched_keywords / total_keywords    */
    const char* response;     /* points into flash; do not free or modify    */
};

/* Initialize the engine. Currently just zeros the per-intent "last
 * response index" tracker so the no-immediate-repeat picker starts fresh.
 * Safe to call multiple times. Idempotent. */
void dialogue_init();

/* Classify `user_text` and pick a response.
 *
 * IMPORTANT: `user_text` is MUTATED in place by the normalizer (lowercased,
 * punctuation replaced with spaces, then split into null-terminated tokens).
 * Callers that need to keep the original string must copy before calling.
 *
 * Returns a DialogueResult whose .response pointer remains valid for the
 * lifetime of the program (points into the static intent tables in flash).
 *
 * Never returns nullptr in .response — the FALLBACK intent always has at
 * least one canned reply available.
 */
DialogueResult dialogue_respond(char* user_text);

#endif /* COSMO_DIALOGUE_H */
