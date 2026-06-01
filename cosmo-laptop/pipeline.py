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
                response = self._picker.pick(intent)
            except (KeyError, ValueError) as exc:
                print(f"[Dialogue] Response picker failed: {exc}")
                self._safe_set_idle()
                continue

            try:
                if self._tts:
                    wav_path = self._tts.synthesize(response)
                else:
                    print(f"[Dialogue] {response}")
                    wav_path = None
            except (TtsError, FileNotFoundError, ValueError) as exc:
                print(f"[TTS] Synthesis failed: {exc}")
                print(f"[Dialogue] {response}")
                wav_path = None

            expression = _expression_for_intent(intent)
            try:
                self._cozmo.set_face(expression)
                if wav_path and os.path.exists(wav_path):
                    self._cozmo.play_wav(wav_path)
                self._cozmo.set_face("idle")
            except CozmoActionError as exc:
                print(f"[Cozmo] Playback failed: {exc}")
                self._safe_set_idle()
            finally:
                if wav_path and os.path.exists(wav_path):
                    os.remove(wav_path)

    def _safe_set_idle(self) -> None:
        try:
            self._cozmo.set_face("idle")
        except CozmoActionError:
            return


def _expression_for_intent(intent_name: str) -> str:
    mapping = {
        "GREETING": "happy",
        "GOODBYE": "happy",
        "IDENTITY": "thinking",
        "HELP": "thinking",
        "HOW_ARE_YOU": "happy",
        "JOKE": "happy",
        "THANKS": "happy",
        "AFFIRMATION": "happy",
        "NEGATION": "sad",
        "COMPLIMENT": "happy",
        "APOLOGY": "sad",
        "LOVE": "happy",
        "INSULT": "sad",
        "PLAY": "happy",
        "FALLBACK": "thinking",
    }
    return mapping.get(intent_name, "thinking")


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
