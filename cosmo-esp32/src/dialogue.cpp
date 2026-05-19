/**
 * dialogue.cpp — Implementation of the Cosmo intent classifier and
 *                response picker. See dialogue.h for the API contract.
 * ============================================================================
 * Implementation notes
 * --------------------
 *   - Memory: zero heap. The only mutable state is a small int array of
 *     size INTENT_COUNT for the no-repeat tracker.
 *   - CPU: the classifier is O(N_intents × N_keywords × N_tokens) with
 *     strcmp at the inner loop. For our scale (≤ 11 intents, ≤ 10 keywords
 *     each, ≤ 16 tokens of input) that's ~1700 strcmps worst case, well
 *     under 100 µs on the C3 at 160 MHz.
 *   - RNG: esp_random() is the ESP32-C3 hardware RNG. No seeding required;
 *     it draws from on-chip entropy sources.
 * ============================================================================
 */

#include "dialogue.h"

#include <Arduino.h>      /* for esp_random() prototype via the IDF headers  */
#include <ctype.h>
#include <string.h>

/* ---- Tunables ------------------------------------------------------------ */

/* Maximum tokens we'll consider per utterance. Inputs longer than this just
 * get truncated — the classifier doesn't benefit from more context, since
 * each intent only has ~5-10 keywords and we already match the strongest
 * ones in the first few tokens of typical input. */
#define DIALOGUE_MAX_TOKENS  16

/* Bound the no-repeat retry loop. With 4 retries and ≥ 3 response options,
 * the probability of all 4 rolls landing on the previous index is ≤ 1/81. */
#define DIALOGUE_MAX_PICK_ATTEMPTS  4

/* ---- Engine state -------------------------------------------------------- */

/* Per-intent last response index used, so the picker can avoid immediate
 * repeats. -1 means "no prior response", any index value picks differently
 * on the next call. Reset to -1 by dialogue_init(). */
static int s_last_response_idx[INTENT_COUNT];

/* ========================================================================== */
/* Normalization & tokenization                                               */
/* ========================================================================== */

/* In-place: lowercase every alphanumeric character; replace everything
 * else (punctuation, underscores, smart quotes, control chars) with a
 * single space so the tokenizer treats it as a separator. */
static void normalize_inplace(char* s)
{
    for (char* p = s; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) {
            *p = (char)tolower(c);
        } else {
            *p = ' ';
        }
    }
}

/* Split the already-normalized `s` into up to DIALOGUE_MAX_TOKENS substrings.
 * Tokens point into `s` and are null-terminated in place. Returns the
 * number of tokens written. `s` is mutated; do not rely on its original
 * contents after this call. */
static int tokenize(char* s, const char* tokens[DIALOGUE_MAX_TOKENS])
{
    int count = 0;
    char* p = s;
    while (*p && count < DIALOGUE_MAX_TOKENS) {
        while (*p == ' ') ++p;                /* skip leading spaces       */
        if (!*p) break;
        tokens[count++] = p;
        while (*p && *p != ' ') ++p;          /* walk to end of token      */
        if (*p) { *p = '\0'; ++p; }           /* null-terminate, advance   */
    }
    return count;
}

/* ========================================================================== */
/* Scoring                                                                    */
/* ========================================================================== */

static bool tokens_contain(const char* tokens[], int n, const char* word)
{
    for (int i = 0; i < n; ++i) {
        if (strcmp(tokens[i], word) == 0) return true;
    }
    return false;
}

/* Returns the percentage [0..100] of `row.keywords` that appear in
 * `tokens`. Intent rows with a NULL keyword list (FALLBACK) score 0 — by
 * design, since FALLBACK is reached via the confidence floor, not direct
 * keyword matching. */
static uint8_t score_intent(const IntentRow& row,
                            const char* tokens[], int n)
{
    if (row.keywords == nullptr) return 0;

    int matched = 0;
    int total   = 0;
    for (const char* const* kw = row.keywords; *kw != nullptr; ++kw) {
        ++total;
        if (tokens_contain(tokens, n, *kw)) ++matched;
    }
    if (total == 0) return 0;
    return (uint8_t)((matched * 100) / total);
}

/* ========================================================================== */
/* Response picking                                                           */
/* ========================================================================== */

static int count_responses(const IntentRow& row)
{
    if (row.responses == nullptr) return 0;
    int n = 0;
    while (row.responses[n] != nullptr) ++n;
    return n;
}

/* Pick a response index for `intent`, avoiding the immediately previous
 * one when more than one option exists. Returns -1 if the intent has no
 * responses at all (should never happen given the table invariants). */
static int pick_response(Intent intent)
{
    int total = count_responses(kIntentTable[intent]);
    if (total <= 0) return -1;
    if (total == 1) return 0;

    int last = s_last_response_idx[intent];
    int idx  = 0;
    for (int attempt = 0; attempt < DIALOGUE_MAX_PICK_ATTEMPTS; ++attempt) {
        idx = (int)(esp_random() % (uint32_t)total);
        if (idx != last) break;
    }
    s_last_response_idx[intent] = idx;
    return idx;
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

void dialogue_init()
{
    for (int i = 0; i < INTENT_COUNT; ++i) {
        s_last_response_idx[i] = -1;
    }
}

DialogueResult dialogue_respond(char* user_text)
{
    DialogueResult out = { INTENT_FALLBACK, 0, nullptr };

    /* Defensive: empty or null input goes straight to fallback. */
    if (user_text == nullptr || user_text[0] == '\0') {
        int idx = pick_response(INTENT_FALLBACK);
        out.response = (idx >= 0)
                       ? kIntentTable[INTENT_FALLBACK].responses[idx]
                       : "";
        return out;
    }

    normalize_inplace(user_text);

    const char* tokens[DIALOGUE_MAX_TOKENS];
    int nt = tokenize(user_text, tokens);

    /* Score every intent except FALLBACK (which has no keywords and
     * therefore can't legitimately "win"). Track the high score. */
    uint8_t best_score  = 0;
    Intent  best_intent = INTENT_FALLBACK;
    for (int i = 0; i < INTENT_COUNT; ++i) {
        if ((Intent)i == INTENT_FALLBACK) continue;
        uint8_t s = score_intent(kIntentTable[i], tokens, nt);
        if (s > best_score) {
            best_score  = s;
            best_intent = (Intent)i;
        }
    }

    /* If even the best match didn't clear the confidence floor, route to
     * fallback so we don't ship a confidently-wrong reply. */
    Intent chosen = (best_score >= DIALOGUE_MIN_CONFIDENCE_PCT)
                      ? best_intent : INTENT_FALLBACK;

    int ridx       = pick_response(chosen);
    out.intent     = chosen;
    out.confidence = best_score;
    out.response   = (ridx >= 0)
                     ? kIntentTable[chosen].responses[ridx]
                     : "";
    return out;
}
