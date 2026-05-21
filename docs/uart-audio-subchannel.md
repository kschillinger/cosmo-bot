# UART audio sub-channel (STM32 ← ESP32-C3)

Phase 4 adds a binary audio path that shares the existing UART link
with the Phase 1 text protocol. The ESP32-C3 stores pre-encoded audio
clips in its 4 MB flash, the STM32L476 decodes and plays them back,
and both halves coexist on a single 115200 baud line.

This document explains how.

## Bandwidth budget

The link is 115200 8N1: with start and stop bits that's 11,520 bytes
per second of usable throughput on the wire.

Audio formats that would fit, including a small allowance for framing
overhead:

| Format                          | Byte rate | Fits at 115200? |
|---------------------------------|-----------|-----------------|
| 8 kHz / 8-bit μ-law mono        |  8.0 KB/s | yes, ~30% headroom |
| **16 kHz / 4-bit IMA ADPCM mono** |  8.0 KB/s | yes, ~30% headroom |
| 8 kHz / 16-bit raw PCM mono     | 16.0 KB/s | no |
| 16 kHz / 16-bit raw PCM mono    | 32.0 KB/s | no |

ADPCM wins. Same byte rate as μ-law but twice the sample rate, which
means real bandwidth up to 8 kHz instead of 4 kHz — wider voice tone,
more natural sibilance, better intelligibility. The decoder is about
fifty lines of C.

Raising the baud rate would open up raw PCM (921600 → 92 KB/s, plenty
for 16-bit / 16 kHz mono), but it touches Phase 1's configured link
rate and would need both MCUs reconfigured and re-tested. Deferred
until something actually needs more bandwidth than ADPCM.

## Wire format

Two parsers share the same byte stream. The text protocol stays exactly
as Phase 1 left it — `[USER_INPUT: ...]\r\n` and `[BOT_RESPONSE: ...]\r\n`
are all ASCII printable plus CR/LF. Binary frames use SLIP-style framing
keyed on the byte `0xC0`, which cannot appear in valid ASCII text:

```
        0x20..0x7E, 0x0D, 0x0A, 0x09       → text-line parser
        0xC0                                → binary-frame parser
        anything else (during text mode)    → ignore + log
```

The text parser keeps its existing per-line state machine. The binary
parser activates on `0xC0`, consumes one frame, and returns control.

### SLIP escaping

Inside a binary frame the byte stream is escape-decoded:

| Byte sequence | Decoded         |
|---------------|-----------------|
| `0xC0`        | (end of frame)  |
| `0xDB 0xDC`   | literal `0xC0`  |
| `0xDB 0xDD`   | literal `0xDB`  |
| any other     | itself          |

Worst-case bloat is 2× (if the payload were all `0xC0` and `0xDB`);
typical bloat on ADPCM audio is well under 1%.

### Frame layout

After SLIP-decoding, every frame has the same skeleton:

```
+------+------+-------+------+ . . . +-------+
| TYPE | SEQ  |  LEN  |  PAYLOAD     | CRC16 |
| 1 B  | 1 B  | 2B LE |  LEN bytes   | 2B LE |
+------+------+-------+------+ . . . +-------+
```

- `TYPE` — frame type, see table below.
- `SEQ` — rolling sequence number, wraps at 256. Used to detect drops,
  not for retransmit (see Reliability).
- `LEN` — payload length, little-endian.
- `PAYLOAD` — `LEN` bytes, type-dependent.
- `CRC16` — CRC-16/CCITT-FALSE, polynomial `0x1021`, init `0xFFFF`,
  computed over `TYPE` + `SEQ` + `LEN` + `PAYLOAD` (header included,
  CRC bytes excluded).

Frame types:

| Code   | Name        | Direction      | Payload |
|--------|-------------|----------------|---------|
| `0x01` | AUDIO_START | ESP32 → STM32  | clip header (below) |
| `0x02` | AUDIO_CHUNK | ESP32 → STM32  | ADPCM block (below) |
| `0x03` | AUDIO_END   | ESP32 → STM32  | 1 B `clip_id` |
| `0x04` | AUDIO_ACK   | STM32 → ESP32  | 1 B last-consumed `seq` (optional flow control) |

## Clip framing

A complete clip is `AUDIO_START` → 1..N × `AUDIO_CHUNK` → `AUDIO_END`.
Chunks carry a sequence number that increments per chunk so the
playback side can detect drops.

### AUDIO_START payload

```
+---------+--------------+----------+---------------+
| CLIP_ID | SAMPLE_RATE  | FMT_CODE | TOTAL_CHUNKS  |
|  1 B    |   2 B LE     |   1 B    |    2 B LE     |
+---------+--------------+----------+---------------+
```

`CLIP_ID` lets the playback side disambiguate if overlapping playback
ever ships (it doesn't today). `FMT_CODE = 0x01` means IMA ADPCM 4-bit
mono; future codes leave room for raw PCM and others.

### AUDIO_CHUNK payload

Each chunk is a self-contained IMA ADPCM block. Decoder state restarts
at every chunk, which means a dropped chunk loses exactly one chunk's
worth of audio and the next chunk decodes cleanly without desync.

```
+-----------+-----------+-------+-----------------+
| PREDICTOR |  STEP_IDX | RSVD  |  ADPCM NIBBLES  |
|  2 B LE   |    1 B    |  1 B  |   even count    |
+-----------+-----------+-------+-----------------+
```

- `PREDICTOR` — int16 LE, the initial sample value (also the first
  output sample of this chunk).
- `STEP_IDX` — 0..88, index into the IMA step-size table.
- `RSVD` — must be `0x00`.
- `ADPCM_NIBBLES` — packed 4-bit deltas, low nibble of each byte
  decoded first.

Recommended chunk size: 30–60 ms of audio. At 16 kHz that's 480–960
samples, or 240–480 bytes of nibble payload after the 4-byte block
header. Pick an even sample count so the trailing byte isn't
half-empty.

## Coexistence with the text protocol

The text protocol layer is untouched. The receiver state machine looks
at each incoming byte and routes by mode:

```c
while (uart_rx_available()) {
    uint8_t b = uart_rx_byte();
    switch (state) {
    case STATE_LINE:
        if (b == 0xC0) { state = STATE_BIN_FRAME; bin_reset(); }
        else            line_feed_byte(b);
        break;
    case STATE_BIN_FRAME:
        if (b == 0xC0) { bin_dispatch_frame(); state = STATE_LINE; }
        else            bin_consume_byte(b);
        break;
    }
}
```

That's the entire demux. The text parser never sees a `0xC0`, the
binary parser never sees a stray ASCII byte mid-frame, and the
transition is one branch deep.

A `[BOT_RESPONSE: ...]` line and its associated audio clip are sent
back-to-back from the ESP32: text line first (so the OLED can render
immediately while audio decodes), then `AUDIO_START`, chunks, and
`AUDIO_END`.

## Reliability

UART has no native error correction. The strategy is **drop on CRC
fail, never retransmit**.

The reasoning:

- Audio is intrinsically lossy. A 30 ms gap is far cheaper than a
  retransmit round-trip plus the buffering required to wait for it.
- Retransmit needs the STM32 to hold every chunk until ACKed. With a
  60 ms latency budget that's only a couple of KB, but it complicates
  the decoder pipeline and doesn't make audio meaningfully better.
- Self-contained ADPCM blocks mean a dropped chunk does not desync
  the decoder. The next chunk starts fresh from its own predictor.

The receiver tracks the chunk sequence number and logs drops to the
debug UART. Optionally it can insert a silent (or repeated-last) chunk
into the playback buffer to keep timing aligned; that's a playback-side
detail, not part of the wire protocol.

## Flow control

The audio rate is 8 KB/s, the wire delivers 11.5 KB/s, and the STM32
decodes ADPCM faster than UART delivers it. Open-loop pacing on the
sender side is sufficient under normal operation: the ESP32 sends
chunks as fast as the UART will take them and the STM32 keeps up.

The optional `AUDIO_ACK` frame lets the STM32 nudge the ESP32 to slow
down if its playback buffer is filling — for example if a long clip
arrives faster than the DAC drains it. Send `AUDIO_ACK` periodically
with the last-consumed sequence number; the ESP32 pauses if the gap
between sent and acknowledged exceeds a tunable threshold (say 16
chunks ≈ 500 ms of buffered audio).

Hardware RTS/CTS would be the simpler alternative if both boards had
the flow-control lines wired through, but they don't on the current
Phase 1 hookup (PA9/PA10 ↔ GPIO5/GPIO4 only), and adding them would
be a wiring change.

## ADPCM specifics

IMA ADPCM packs each 16-bit sample into a 4-bit delta against an
adaptive predictor. The decoder is two small tables and a few lines
of code:

```c
static const int8_t  ima_index_table[16] = {
    -1, -1, -1, -1,  2,  4,  6,  8,
    -1, -1, -1, -1,  2,  4,  6,  8
};
static const int16_t ima_step_table[89] = { /* 7, 8, 9, ... 32767 */ };

static int16_t predictor;
static uint8_t step_index;

int16_t ima_decode_nibble(uint8_t nib)
{
    int16_t step = ima_step_table[step_index];
    int32_t diff = step >> 3;
    if (nib & 4) diff += step;
    if (nib & 2) diff += step >> 1;
    if (nib & 1) diff += step >> 2;
    if (nib & 8) predictor -= diff; else predictor += diff;
    if (predictor >  32767) predictor =  32767;
    if (predictor < -32768) predictor = -32768;
    step_index += ima_index_table[nib];
    if (step_index > 88) step_index = 88;
    if ((int8_t)step_index < 0) step_index = 0;
    return predictor;
}
```

Total flash cost for the decoder plus the two tables is under 300
bytes. RAM cost is two static variables. The encoder runs offline on
the workstation when audio clips are generated — the firmware never
encodes anything.

## Clip storage on the ESP32

Clips live in the ESP32-C3's 4 MB flash as a packed asset bundle.
Two reasonable formats:

- **Generated header file** with a `const uint8_t clip_<name>[]` array
  per clip and a string-keyed lookup table. Builds clip selection
  right into the binary, zero runtime parsing.
- **Custom partition** with a small TOC followed by raw ADPCM blocks.
  Lets clips be updated without re-flashing firmware.

The header-array approach is simpler and is the right starting point.
Switch to a partition when clip count grows past a few dozen or when
over-the-air clip updates become a goal.

The mapping from a chosen response variant to a clip is the other open
piece. The simplest correspondence is a parallel array next to each
intent's `responses[]`: `clip_ids[]`, same length, indexed by the
chosen response's index. That keeps the dialogue engine ignorant of
audio entirely — it picks the response index, the audio layer maps it
to a clip.

## Testing strategy

1. **Loopback on the bench.** Send a known clip from a USB-serial
   adapter into the STM32's RX pin; verify decoded audio matches a
   reference WAV byte-for-byte at the framebuffer.
2. **Frame torture.** Inject malformed frames (bad CRC, truncated
   length, escape mid-frame) and confirm the parser recovers cleanly
   on the next `0xC0`.
3. **Coexistence.** Stream audio chunks back-to-back with periodic
   text lines and verify both decode correctly with no cross-talk.
4. **End-to-end.** ESP32 sends a complete clip in response to a real
   `[USER_INPUT: ...]`; STM32 decodes and plays through the audio
   output.
5. **Long-run.** Stream continuous audio for 10+ minutes and check
   for drift, buffer overflow, or CRC accumulation issues.

## Deferred / open questions

- **Encoder pipeline.** Out of scope here. Will be a workstation tool
  that takes WAV → IMA ADPCM blocks, written when clip authoring
  starts.
- **Output stage on the STM32.** DAC channel 1 with DMA? PWM at a high
  carrier? I²S to an external DAC? Either works; choice depends on
  the speaker amp hardware. The decoder API ends at "samples in a
  ring buffer"; the output stage drains from there at 16 kHz.
- **Multiple simultaneous clips / mixing.** Single-clip playback only
  for now. The `CLIP_ID` field in `AUDIO_START` leaves room.
- **Variable sample rate per clip.** Each clip's header has a
  `SAMPLE_RATE` field, but the decoder will assume 16 kHz initially.
  Honoring per-clip rates is a small change once it matters.
- **Baud-rate bump path.** If a future clip needs higher fidelity,
  raise to 921600 baud and add a raw-PCM `FMT_CODE`. Both protocol
  and decoder are ready; only the link configuration changes.
