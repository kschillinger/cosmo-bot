/**
 * @file    response_database.c
 * @brief   Storage definition for the response lookup table.
 *
 * Each row binds the exact response string emitted by the dialogue engine to
 * the embedded PCM clip generated from its source .wav file. The text MUST
 * match byte-for-byte (including emoji bytes) for the fast O(1) equality
 * lookup; the fuzzy fallback in tts_find_response() handles minor drift.
 */

#include "tts.h"
#include "response_clips.h"

const response_clip_t response_database[] = {

    /* ── GREETING ────────────────────────────────────────────────────── */
    {
        .response_text   = "Hey there! I'm so excited to meet you! 😊",
        .audio_data      = greeting_001_audio,
        .audio_size      = greeting_001_size,
        .duration_ms     = greeting_001_duration_ms,
        .source_filename = "responses/greeting_001.wav"
    },
    {
        .response_text   = "Hi! Great to see you! What's on your mind?",
        .audio_data      = greeting_002_audio,
        .audio_size      = greeting_002_size,
        .duration_ms     = greeting_002_duration_ms,
        .source_filename = "responses/greeting_002.wav"
    },
    {
        .response_text   = "Hello, friend! I'm Cosmo, and I'm pumped to chat with you!",
        .audio_data      = greeting_003_audio,
        .audio_size      = greeting_003_size,
        .duration_ms     = greeting_003_duration_ms,
        .source_filename = "responses/greeting_003.wav"
    },

    /* ── JOKE_REQUEST ────────────────────────────────────────────────── */
    {
        .response_text   = "Why did the robot go to school? To get a little smarter! 🤖",
        .audio_data      = joke_001_audio,
        .audio_size      = joke_001_size,
        .duration_ms     = joke_001_duration_ms,
        .source_filename = "responses/joke_001.wav"
    },
    {
        .response_text   = "I tried to tell a programming joke once, but nobody got it...",
        .audio_data      = joke_002_audio,
        .audio_size      = joke_002_size,
        .duration_ms     = joke_002_duration_ms,
        .source_filename = "responses/joke_002.wav"
    },
    {
        .response_text   = "What do you call a robot comedian? A pun-droid! 😄",
        .audio_data      = joke_003_audio,
        .audio_size      = joke_003_size,
        .duration_ms     = joke_003_duration_ms,
        .source_filename = "responses/joke_003.wav"
    },

    /* ── GOODBYE ─────────────────────────────────────────────────────── */
    {
        .response_text   = "Bye! Come back soon, okay? 👋",
        .audio_data      = goodbye_001_audio,
        .audio_size      = goodbye_001_size,
        .duration_ms     = goodbye_001_duration_ms,
        .source_filename = "responses/goodbye_001.wav"
    },
    {
        .response_text   = "See you later! Don't forget about me!",
        .audio_data      = goodbye_002_audio,
        .audio_size      = goodbye_002_size,
        .duration_ms     = goodbye_002_duration_ms,
        .source_filename = "responses/goodbye_002.wav"
    },

    /* ── FALLBACK / ERROR ────────────────────────────────────────────── */
    {
        .response_text   = "Hmm, I didn't quite catch that. Can you say it again? 🤔",
        .audio_data      = fallback_001_audio,
        .audio_size      = fallback_001_size,
        .duration_ms     = fallback_001_duration_ms,
        .source_filename = "responses/fallback_001.wav"
    },
    {
        .response_text   = "Oops, something went wrong! Let's try again!",
        .audio_data      = error_001_audio,
        .audio_size      = error_001_size,
        .duration_ms     = error_001_duration_ms,
        .source_filename = "responses/error_001.wav"
    }
};

const uint32_t response_database_size =
    sizeof(response_database) / sizeof(response_database[0]);
