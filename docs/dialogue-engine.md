# Dialogue engine (ESP32-C3)

The ESP32-C3 receives recognized user utterances over UART from the STM32
and must respond in Cosmo's voice. This document explains how it does
that, why it is built this way, and how to extend it.

## Why a keyword classifier?

For a closed-domain assistant with around ten intents, more sophisticated
approaches (bag-of-words cosine similarity, embeddings, a tiny on-device
LLM) cost a lot more memory and CPU than they are worth. A keyword-overlap
classifier:

- Fits in a few KB of flash, including every intent's data.
- Runs in microseconds on the C3 at 160 MHz.
- Is trivially debuggable: you can read the intent table and predict
  the output for any input.
- Degrades gracefully: adding a new intent is a one-file edit with no
  retraining, no model file, no compile-time data baking.
- Composes cleanly with the next stage: once STT is real, its noisy
  output ("helo cosmoe", "whoo arr yu") survives the normalizer because
  the keywords are simple and short.

The cost is recall. An input like "Could you possibly let me know who
I am talking to" might not match IDENTITY if the user's phrasing avoids
the keywords. The cure is to expand the keyword list, which is nearly
free.

## Pipeline

```
user_text (raw)
   |
   v
normalize_inplace()      lowercase alphanumeric, replace everything
   |                     else with single spaces
   v
tokenize()               split on spaces; up to 16 substrings pointing
   |                     into the same buffer; null-terminate in place
   v
score_intent() x N       for each intent, count how many of its
   |                     keywords appear in the token set; score is
   |                     matched / total_keywords expressed as 0-100
   v
argmax across all intents (FALLBACK never scored, only reached via floor)
   |
   v
confidence floor check   if best_score < DIALOGUE_MIN_CONFIDENCE_PCT
   |                     (default 25), route to INTENT_FALLBACK
   v
pick_response()          choose a random response variant for the
   |                     chosen intent, avoiding the one used last
   v
DialogueResult { intent, confidence, response }
```

## Scoring math

For each intent I with keyword set K_I and input tokens T:

```
score(I) = |T intersect K_I| / |K_I| * 100
```

This is a Jaccard-style overlap from the intent's perspective: we ask
"what fraction of this intent's signature words did the user say?"
rather than "what fraction of the user's words match this intent?".
That choice matters because long user inputs (after STT mistranscribes
something into 20 tokens) would dilute the user's-perspective version
toward zero. The intent's perspective is stable across input length.

The winner is argmax_I score(I). Ties are broken by enum order
(first-declared wins). If max(score) < DIALOGUE_MIN_CONFIDENCE_PCT
(default 25 percent) we route to INTENT_FALLBACK. FALLBACK itself is
never scored - its keyword list is nullptr - so it is reached only via
the confidence floor, never by accident.

### Why 25 percent by default?

Most intents have 4-8 keywords. A single confident match is 12-25
percent, two matches is 25-50 percent. Setting the floor at 25 percent
means "at least one strong keyword plus weak corroboration, or two
clear hits" before classifying. Higher floors (40-50) suppress more
weak guesses but also reject genuine matches when the user is terse.
Tune to taste.

Override at build time:

```ini
build_flags =
    -D DIALOGUE_MIN_CONFIDENCE_PCT=35
```

## Response selection

Each intent has 3-5 response variants. The picker uses esp_random()
(the ESP32-C3's hardware random number generator, no seeding required)
and avoids the immediately previous response when the intent has more
than one option. Up to 4 attempts; if all four somehow land on the
previous index (probability under 1/81 with 3 options, vanishing with
more) the fourth roll is accepted to bound the loop.

The "no immediate repeat" rule is per-intent, not global. Saying "hi"
twice in a row will produce two different greetings, but it will not
remember which greeting it used three turns ago. This is intentional:
human users do not notice greetings repeating across a multi-minute
conversation, but back-to-back repeats feel mechanical.

## Adding an intent

1. Append to the Intent enum in intents.h, before INTENT_FALLBACK:

   ```cpp
   enum Intent : uint8_t {
       INTENT_GREETING,
       // ...
       INTENT_COMPLIMENT,
       INTENT_WEATHER,      // new, added before fallback
       INTENT_FALLBACK,
       INTENT_COUNT
   };
   ```

2. Define the keyword list and response list in intents.cpp:

   ```cpp
   static const char* const kw_weather[] = {
       "weather", "temperature", "rain", "snow", "sunny", "cold", "hot",
       nullptr
   };

   static const char* const resp_weather[] = {
       "I'm indoors, but you look warm-blooded enough.",
       "My weather sensor is broken. Probably nice though.",
       "It's always 80 megahertz in here.",
       nullptr
   };
   ```

3. Insert the row into kIntentTable[] in the same order as the enum:

   ```cpp
   /* INTENT_COMPLIMENT */ { "COMPLIMENT", kw_compliment, resp_compliment },
   /* INTENT_WEATHER    */ { "WEATHER",    kw_weather,    resp_weather    },
   /* INTENT_FALLBACK   */ { "FALLBACK",   nullptr,       resp_fallback   },
   ```

That is the entire change. Rebuild and the classifier picks it up.

## Tuning guide

- Distinctive keywords beat common ones. Avoid putting "you", "is",
  "are", "the" in any list. They appear in too many sentences and
  either pull noise into a high-scoring intent or get drowned out by
  tighter intents.
- Prefer the root form plus an explicit variant ("thank" and "thanks").
  We do not do stemming. Two tokens is cheap; missed matches are not.
- Use the debug log to inspect classifications during development.
  Every call prints the chosen intent, confidence, and response, plus
  the parsed text.
- The classifier is symmetric in token order: "hello there" and
  "there hello" score identically. Fine for now, revisit if intent
  collisions become a real problem.

## Statelessness and what comes next

dialogue_respond() is a pure function of its input plus esp_random()'s
stream and the saved "last response index per intent" array. There is
no conversation memory yet: no user name remembered, no topic threading,
no follow-ups.

That is deliberately deferred. The current STM32 FSM is single-shot
(one button press = one round of LISTENING through IDLE). Adding memory
means defining what counts as a "session" and how it expires, better
solved once real STT is wired up and we see the actual mistakes the
pipeline makes. Premature memory plumbing tends to model the wrong
thing.

When that day comes, the natural extension points are:

- A DialogueContext struct passed into dialogue_respond(), holding the
  user's name and the last K (intent, text) pairs.
- An optional context_handler field on IntentRow for intents that want
  to consume context (e.g. an intent that introduces the user's name
  and another that recalls it).
- A session timeout managed by the STM32 FSM and signaled to the ESP32
  via a [RESET_SESSION] line on the UART.

None of this changes the keyword-overlap classifier itself. It stays
the fast first-pass router, with context-aware logic layered on top of
the intents it routes into.
