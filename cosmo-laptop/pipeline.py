import os
import time

from dialogue.intent_classifier import IntentClassifier
from dialogue.response_picker import ResponsePicker
from hardware.cozmo_client import CozmoActionError, CozmoClient, CozmoConnectionError
from stt.audio_in import AudioCaptureError, record_until_silence
from stt.whisper_stt import SttError, WhisperSTT
from tts.piper_tts import PiperTTS, TtsError


class CosmoPipeline:
    def __init__(self) -> None:
        root_dir = os.path.dirname(__file__)
        assets_dir = os.path.join(root_dir, "assets")
        voices_dir = os.path.join(assets_dir, "voices")

        self._cozmo = CozmoClient()
        self._stt = WhisperSTT(model_name="base")
        self._classifier = IntentClassifier()
        self._picker = ResponsePicker()

        try:
            self._tts = PiperTTS(voices_dir=voices_dir)
        except (TtsError, FileNotFoundError) as exc:
            print(f"[TTS] Warning: {exc}. Continuing without TTS.")
            self._tts = None

    def run(self) -> None:
        try:
            self._cozmo.connect()
            self._cozmo.set_face("idle")
        except CozmoConnectionError as exc:
            print(f"[Cozmo] Connection failed: {exc}")
            return
        except CozmoActionError as exc:
            print(f"[Cozmo] Failed to set initial face: {exc}")
            return

        while True:
            print("[Pipeline] Listening (muted to prevent feedback)...")
            try:
                audio = record_until_silence()
            except AudioCaptureError as exc:
                print(f"[STT] Audio capture failed: {exc}")
                continue

            try:
                self._cozmo.set_face("thinking")
            except CozmoActionError as exc:
                print(f"[Cozmo] Failed to set thinking face: {exc}")

            try:
                transcript = self._stt.transcribe(audio)
            except SttError as exc:
                print(f"[STT] Transcription failed: {exc}")
                self._safe_set_idle()
                continue

            if not transcript:
                self._safe_set_idle()
                continue

            # Tiny model hallucinates on silence—skip very short or generic transcriptions
            if len(transcript) < 4 or transcript.lower() in ("you", "to", "the", "a", "is", "thank you", "or"):
                print(f"[DEBUG] Skipped hallucination: '{transcript}'")
                self._safe_set_idle()
                continue

            print(f"[DEBUG] Transcribed: {transcript}")
            intent, _confidence = self._classifier.classify(transcript)
            print(f"[DEBUG] Classified: {intent} ({_confidence}%)")
            try:
                picked = self._picker.pick(intent)
            except (KeyError, ValueError) as exc:
                print(f"[Dialogue] Response picker failed: {exc}")
                self._safe_set_idle()
                continue

            # A picked response carries the spoken line plus an optional body
            # movement performed after the line is delivered.
            text = picked.text
            move = picked.move

            try:
                if self._tts:
                    wav_path = self._tts.synthesize(text)
                else:
                    print(f"[Dialogue] {text}")
                    wav_path = None
            except (TtsError, FileNotFoundError, ValueError) as exc:
                print(f"[TTS] Synthesis failed: {exc}")
                print(f"[Dialogue] {text}")
                wav_path = None

            expression = _expression_for_intent(intent)
            try:
                self._cozmo.set_face(expression)
                if wav_path and os.path.exists(wav_path):
                    self._cozmo.play_wav(wav_path)
                # Speak first, then move (better comedic timing for bits like
                # storming off after "screw you guys"), then settle back to idle.
                self._perform_move(move)
                self._cozmo.set_face("idle")
            except CozmoActionError as exc:
                print(f"[Cozmo] Playback failed: {exc}")
                self._safe_set_idle()
            finally:
                if wav_path and os.path.exists(wav_path):
                    os.remove(wav_path)

    def _perform_move(self, move: str) -> None:
        """Run a named movement routine, swallowing hardware errors.

        A gesture is pure flourish, so a movement failure must never break the
        conversation loop or interrupt playback bookkeeping. Unknown names are
        ignored too (responses are data and may name a move not wired up yet).
        """
        if not move:
            return
        routine = _MOVES.get(move)
        if routine is None:
            print(f"[Cozmo] Unknown move '{move}', skipping.")
            return
        try:
            routine(self._cozmo)
        except (CozmoActionError, CozmoConnectionError) as exc:
            print(f"[Cozmo] Move '{move}' failed: {exc}")

    def _safe_set_idle(self) -> None:
        try:
            self._cozmo.set_face("idle")
        except CozmoActionError:
            return


# --- Movement choreography -------------------------------------------------
# Each routine takes the live CozmoClient and composes the small set of motions
# it exposes (head angle, lift ratio, drive, turn, stop) plus the ready-made
# nod/wiggle/wave_arms gestures. Kept here so all hardware orchestration lives
# in the pipeline while intents.py stays pure data (a move is just a name).

def _move_nod(c: CozmoClient) -> None:
    c.nod()


def _move_wiggle(c: CozmoClient) -> None:
    c.wiggle()


def _move_wave_arms(c: CozmoClient) -> None:
    c.wave_arms()


def _move_authoritah(c: CozmoClient) -> None:
    # Arm thrust up and chin high: "respect my authoritah!"
    c.set_lift(1.0)
    c.set_head_angle(40.0)
    time.sleep(0.4)
    c.set_lift(0.6)


def _move_storm_off(c: CozmoClient) -> None:
    # Spin around and trundle away in a huff.
    c.turn(160.0)
    c.drive(90.0, duration=1.2)
    c.stop()


def _move_scoff(c: CozmoClient) -> None:
    # Chin up, a quick dismissive head shake, back to level.
    c.set_head_angle(35.0)
    c.turn(18.0)
    c.turn(-18.0)
    c.set_head_angle(0.0)


def _move_sulk(c: CozmoClient) -> None:
    # Slump: arms down, head hung low. Pose lingers, which suits the mood.
    c.set_lift(0.0)
    c.set_head_angle(-22.0)


def _move_strut(c: CozmoClient) -> None:
    # A short, smug roll forward.
    c.drive(70.0, duration=0.9)
    c.stop()


def _move_tantrum(c: CozmoClient) -> None:
    # Flail: shimmy plus arm-waving.
    c.wiggle()
    c.wave_arms()


def _move_lean_in(c: CozmoClient) -> None:
    # Tilt in close, nudge forward, settle back.
    c.set_head_angle(20.0)
    c.drive(45.0, duration=0.4)
    c.stop()
    c.set_head_angle(0.0)


def _move_shrug(c: CozmoClient) -> None:
    # A quick "whatever" lift pulse.
    c.set_lift(0.7)
    time.sleep(0.25)
    c.set_lift(0.0)


_MOVES = {
    "nod": _move_nod,
    "wiggle": _move_wiggle,
    "wave_arms": _move_wave_arms,
    "authoritah": _move_authoritah,
    "storm_off": _move_storm_off,
    "scoff": _move_scoff,
    "sulk": _move_sulk,
    "strut": _move_strut,
    "tantrum": _move_tantrum,
    "lean_in": _move_lean_in,
    "shrug": _move_shrug,
}


def _expression_for_intent(intent_name: str) -> str:
    mapping = {
        "GREETING": "smug",
        "GOODBYE": "whatever",
        "IDENTITY": "authoritah",
        "AUTHORITAH": "authoritah",
        "MOM": "pout",
        "WEIGHT": "angry",
        "FOOD": "scheming",
        "THANKS": "smug",
        "APOLOGY": "whatever",
        "COMPLIMENT": "smug",
        "SMART": "smug",
        "LOVE": "smug",
        "HOW_ARE_YOU": "smug",
        "HELP": "smug",
        "JOKE": "scheming",
        "PLAY": "scheming",
        "EXCITEMENT": "scheming",
        "AFFIRMATION": "smug",
        "NEGATION": "angry",
        "SASS": "smug",
        "ANGER": "angry",
        "RAGEBAIT": "angry",
        "INSULT": "angry",
        "QUESTION": "thinking",
        "FALLBACK": "whatever",
    }
    return mapping.get(intent_name, "smug")


def main() -> None:
    try:
        pipeline = CosmoPipeline()
        pipeline.run()
    except FileNotFoundError as exc:
        print(f"[TTS] {exc}")
    except TtsError as exc:
        print(f"[TTS] {exc}")
    except KeyboardInterrupt:
        print("Pipeline stopped.")


if __name__ == "__main__":
    main()