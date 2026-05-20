/**
 * intents.h — Intent classification taxonomy for Cosmo Bot
 * ============================================================================
 * Defines the closed set of conversational intents that the dialogue engine
 * recognizes. Each intent corresponds to:
 *   - a list of trigger keywords (used by the classifier)
 *   - a list of canned response variants (used by the response selector)
 *
 * Adding a new intent
 * -------------------
 *   1. Append a new entry to the `Intent` enum below, *before* INTENT_COUNT.
 *      Insert it before INTENT_FALLBACK — fallback must remain the last real
 *      intent so it stays as the safety net.
 *   2. Add a matching row to kIntentTable[] in intents.cpp, in the same order
 *      as the enum.
 *   3. Define the static const char* const arrays (keywords, responses) that
 *      the new row points to. Rebuild — the classifier and response picker
 *      pick the intent up automatically.
 *
 * Why a keyword classifier?
 * -------------------------
 * Bag-of-words / cosine / embeddings / tiny-LLM would cost far more memory
 * and CPU than they're worth for ~10 closed-domain intents. A keyword-
 * overlap classifier fits in a few KB of flash, runs in microseconds, and
 * is trivially debuggable — you can read the intent table and predict the
 * output.
 *
 * See docs/dialogue-engine.md for the design rationale and scoring math.
 * ============================================================================
 */
#ifndef COSMO_INTENTS_H
#define COSMO_INTENTS_H

#include <stddef.h>
#include <stdint.h>

/* The order of these enum values is significant: kIntentTable[i] *must*
 * correspond to the Intent value i. INTENT_FALLBACK must be the last real
 * intent (just before INTENT_COUNT) because the classifier never scores it
 * — it is reached only via the confidence floor in dialogue.cpp. */
enum Intent : uint8_t {
    INTENT_GREETING,
    INTENT_GOODBYE,
    INTENT_IDENTITY,
    INTENT_HELP,
    INTENT_HOW_ARE_YOU,
    INTENT_JOKE,
    INTENT_THANKS,
    INTENT_AFFIRMATION,
    INTENT_NEGATION,
    INTENT_COMPLIMENT,
    INTENT_APOLOGY,
    INTENT_LOVE,
    INTENT_INSULT,
    INTENT_PLAY,
    INTENT_FALLBACK,
    INTENT_COUNT       /* sentinel — number of real intents incl. fallback */
};

/* One row per intent.
 *   - `name` is a short uppercase label used only in debug logging.
 *   - `keywords` is a NULL-terminated array of lowercase trigger tokens.
 *     May be nullptr for INTENT_FALLBACK (which has no keywords by design).
 *   - `responses` is a NULL-terminated array of response strings, randomly
 *     selected at runtime with no-immediate-repeat. Must have ≥ 1 entry.
 *
 * Pointers reference static-storage-duration arrays in intents.cpp — the
 * struct is always valid at runtime, no heap, no lifetime concerns. */
struct IntentRow {
    const char*        name;
    const char* const* keywords;
    const char* const* responses;
};

/* INTENT_COUNT entries, indexed by Intent. Defined in intents.cpp. */
extern const IntentRow kIntentTable[INTENT_COUNT];

/* Printable name for an intent (for debug logging). Returns "?" if the
 * value is out of range. */
const char* intent_name(Intent i);

#endif /* COSMO_INTENTS_H */
