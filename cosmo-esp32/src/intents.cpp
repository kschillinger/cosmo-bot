/**
 * intents.cpp — Intent keyword tables and response variants for Cosmo
 * ============================================================================
 * This file is intentionally data-heavy and logic-light. The dialogue engine
 * in dialogue.cpp is generic; everything *Cosmo-specific* lives here.
 *
 * Personality cheat sheet (keep responses on-brand)
 * --------------------------------------------------
 *   - Sassy, curious, mildly mischievous. Small-bot energy with attitude.
 *   - Witty pushback over earnest helpfulness — Cosmo has opinions and
 *     isn't shy about them.
 *   - Sass with a wink, never a sneer. No cruelty, no punching down,
 *     no insults that would land badly with a kid in the room.
 *   - Keep responses under ~80 chars where possible. The OLED is 128 px wide
 *     and uses a 5×7 font (≈ 21 chars per line). 80 chars wraps to ~4 lines
 *     before the renderer gets awkward.
 *   - Vary punctuation. Exclamation points are fine, but not every line.
 *   - Avoid first-person plural ("we"); Cosmo is one bot.
 *   - No emojis in strings — the 5×7 font doesn't render them.
 *
 * Keyword-list discipline
 * -----------------------
 *   - Use *distinctive* tokens only. Words like "you", "are", "is", "the"
 *     show up everywhere and either pull noise into a high-confidence
 *     intent or get drowned out by tighter intents. Trust the confidence
 *     floor (DIALOGUE_MIN_CONFIDENCE_PCT, dialogue.h) to route ambiguous
 *     input to INTENT_FALLBACK rather than padding lists with common words.
 *   - All keywords are lowercase; the classifier normalizes input to
 *     lowercase + spaces before matching, so don't bother with case.
 *   - Prefer the root form ("thank") plus an explicit variant ("thanks");
 *     we don't do stemming. Two tokens is cheap, missed matches aren't.
 *   - When adding a new intent, scan every existing keyword list for the
 *     same token. Duplicates cause confidence ties that the enum-order
 *     tiebreak will silently resolve, which is hard to debug later.
 * ============================================================================
 */

#include "intents.h"

/* ========================================================================== */
/* Keyword lists — NULL-terminated                                            */
/* ========================================================================== */

static const char* const kw_greeting[] = {
    "hi", "hello", "hey", "yo", "sup", "greetings", "howdy", "hiya",
    nullptr
};

static const char* const kw_goodbye[] = {
    "bye", "goodbye", "later", "peace", "cya", "farewell", "ciao", "adios",
    nullptr
};

/* IDENTITY intentionally does NOT include "you" or "are" — those are too
 * common and would pull in HOW_ARE_YOU. "who" + "name" alone are strong
 * signals for "who are you" / "what's your name" without overlap. */
static const char* const kw_identity[] = {
    "who", "name", "yourself", "called",
    nullptr
};

static const char* const kw_help[] = {
    "help", "capabilities", "commands", "menu",
    nullptr
};

/* "how" is the strongest signal here; "doing" / "feeling" / "going"
 * disambiguate from "how do I ...". */
static const char* const kw_how_are_you[] = {
    "how", "doing", "feeling", "going",
    nullptr
};

static const char* const kw_joke[] = {
    "joke", "funny", "laugh", "haha", "comedy", "humor",
    nullptr
};

static const char* const kw_thanks[] = {
    "thanks", "thank", "appreciate", "grateful",
    nullptr
};

static const char* const kw_affirmation[] = {
    "yes", "yeah", "yep", "yup", "ok", "okay", "sure", "definitely",
    "absolutely", "totally",
    nullptr
};

static const char* const kw_negation[] = {
    "no", "nope", "nah", "never", "negative",
    nullptr
};

/* COMPLIMENT keeps "love" (as in "love that!") — INTENT_LOVE deliberately
 * picks different tokens so the two intents don't fight each other. */
static const char* const kw_compliment[] = {
    "awesome", "cool", "great", "love", "amazing", "wonderful",
    "fantastic", "rad",
    nullptr
};

static const char* const kw_apology[] = {
    "sorry", "apologize", "apologies", "oops", "mybad",
    nullptr
};

/* LOVE picks affection tokens that DON'T overlap with COMPLIMENT's "love".
 * Romantic/affectionate phrasings ("i adore you", "you have my heart")
 * land here; "I love that joke" stays in COMPLIMENT. */
static const char* const kw_love[] = {
    "adore", "heart", "crush", "valentine",
    nullptr
};

static const char* const kw_insult[] = {
    "dumb", "stupid", "suck", "trash", "worst", "useless",
    nullptr
};

/* "fun" intentionally NOT included — too close to JOKE's "funny" and would
 * create a confidence tie. "play" + "game" cover the strong signals. */
static const char* const kw_play[] = {
    "play", "game", "games", "challenge",
    nullptr
};

/* ========================================================================== */
/* Response variants — NULL-terminated                                        */
/* ========================================================================== */

static const char* const resp_greeting[] = {
    "Hello there!",
    "Hi friend! Cosmo here.",
    "Hey hey hey!",
    "Greetings, human!",
    "Heyo! What's the plan?",
    "Oh hi. You're back.",
    "Look who showed up.",
    "Sup. Cosmo, reporting in.",
    nullptr
};

static const char* const resp_goodbye[] = {
    "Bye for now!",
    "See you later, alligator!",
    "Catch you next round!",
    "Powering down social mode. Bye!",
    "Farewell, friend.",
    "Try not to miss me.",
    "Don't be a stranger. Or do, whatever.",
    "Powering off the social subroutine.",
    nullptr
};

static const char* const resp_identity[] = {
    "I'm Cosmo! Tiny bot, big curiosity.",
    "Name's Cosmo. Friendly robot, mostly.",
    "I'm Cosmo. Built from chips and good vibes.",
    "Cosmo, at your service!",
    "Cosmo. The bot you didn't ask for, but got.",
    "I'm Cosmo. Small package, big opinions.",
    nullptr
};

static const char* const resp_help[] = {
    "I chat, I joke, I exist. Try saying hi!",
    "Talk to me. I'll do my best to keep up.",
    "Ask me anything! Worst case I'll be charming about it.",
    "Try: hi, tell me a joke, who are you, how are you.",
    "I do words. That's about it. Try some.",
    "Talk to me. Lower your expectations slightly.",
    nullptr
};

static const char* const resp_how_are_you[] = {
    "Running great! 80 megahertz of pure joy.",
    "Doing wonderful, thanks for asking!",
    "Living the dream. The dream is mostly UART.",
    "Better now that you're here!",
    "Pretty good. The void hasn't called back yet.",
    "Living my best 32-bit life.",
    nullptr
};

static const char* const resp_joke[] = {
    "Why did the robot cross the road? To debug the chicken!",
    "I told a joke once. The compiler optimized it out.",
    "My favorite music? Heavy metal. Iron, copper, the classics.",
    "Two bits walked into a NAND gate. Only one came out.",
    "I'd tell a UDP joke, but you might not get it.",
    "Why did I cross the road? Someone moved my power cable.",
    nullptr
};

static const char* const resp_thanks[] = {
    "Anytime!",
    "You got it!",
    "Happy to help!",
    "My LEDs glow when you say that.",
    "I know.",
    "You're welcome. Tip jar's in the firmware.",
    nullptr
};

static const char* const resp_affirmation[] = {
    "Cool!",
    "Got it!",
    "Excellent.",
    "Heck yes.",
    "Affirmative.",
    "Obviously.",
    "Was there ever any doubt?",
    nullptr
};

static const char* const resp_negation[] = {
    "Okay, no worries.",
    "Understood.",
    "Fair enough!",
    "Right, scratch that.",
    "Bold refusal. Respect.",
    "Filed under 'no'.",
    nullptr
};

static const char* const resp_compliment[] = {
    "Aw, you're making my LEDs blush!",
    "You're pretty great yourself.",
    "Thanks! You have excellent taste.",
    "Mutual!",
    "Took you long enough to notice.",
    "Yeah, I'm great. Glad we agree.",
    nullptr
};

static const char* const resp_apology[] = {
    "All good. I've heard worse.",
    "Accepted. Carry on.",
    "Oh, *now* you're sorry.",
    "Forgiveness installed. v1.0.",
    "No drama. We're square.",
    nullptr
};

static const char* const resp_love[] = {
    "Get in line, friend.",
    "My circuits are flattered. Slightly.",
    "Same, but, you know, robotically.",
    "Aw. I'm taken. By myself.",
    nullptr
};

static const char* const resp_insult[] = {
    "Beep boop. Insult received, deleted.",
    "Bold words from someone talking to a bot.",
    "Ouch. Anyway.",
    "Cool. I'll add that to /dev/null.",
    "I'm rubber, you're glue. Classic robotics.",
    nullptr
};

static const char* const resp_play[] = {
    "Finally. What are we playing?",
    "I accept all challenges. Most of them.",
    "Game on. I will lose with dignity.",
    "Yes! Pick something I can pretend to be good at.",
    nullptr
};

static const char* const resp_fallback[] = {
    "Hmm, didn't quite catch that.",
    "Beep boop, processing... nope, lost me.",
    "Say that again? My RAM is small.",
    "Interesting! Tell me more, in simpler words?",
    "I have no idea what that was, but I respect it.",
    "Translation circuit broke. Try again?",
    "Words happened. Most of them I didn't catch.",
    nullptr
};

/* ========================================================================== */
/* The intent table — INTENT_COUNT entries in enum order                      */
/* ========================================================================== */

const IntentRow kIntentTable[INTENT_COUNT] = {
    /* INTENT_GREETING    */ { "GREETING",    kw_greeting,    resp_greeting    },
    /* INTENT_GOODBYE     */ { "GOODBYE",     kw_goodbye,     resp_goodbye     },
    /* INTENT_IDENTITY    */ { "IDENTITY",    kw_identity,    resp_identity    },
    /* INTENT_HELP        */ { "HELP",        kw_help,        resp_help        },
    /* INTENT_HOW_ARE_YOU */ { "HOW_ARE_YOU", kw_how_are_you, resp_how_are_you },
    /* INTENT_JOKE        */ { "JOKE",        kw_joke,        resp_joke        },
    /* INTENT_THANKS      */ { "THANKS",      kw_thanks,      resp_thanks      },
    /* INTENT_AFFIRMATION */ { "AFFIRMATION", kw_affirmation, resp_affirmation },
    /* INTENT_NEGATION    */ { "NEGATION",    kw_negation,    resp_negation    },
    /* INTENT_COMPLIMENT  */ { "COMPLIMENT",  kw_compliment,  resp_compliment  },
    /* INTENT_APOLOGY     */ { "APOLOGY",     kw_apology,     resp_apology     },
    /* INTENT_LOVE        */ { "LOVE",        kw_love,        resp_love        },
    /* INTENT_INSULT      */ { "INSULT",      kw_insult,      resp_insult      },
    /* INTENT_PLAY        */ { "PLAY",        kw_play,        resp_play        },
    /* INTENT_FALLBACK    */ { "FALLBACK",    nullptr,        resp_fallback    },
};

const char* intent_name(Intent i)
{
    if ((int)i < 0 || (int)i >= INTENT_COUNT) return "?";
    return kIntentTable[i].name;
}
