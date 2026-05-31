from __future__ import annotations

import os
import tempfile
import wave
from typing import Optional

from piper import PiperVoice


class TtsError(RuntimeError):
    """Raised when Piper synthesis fails."""


class PiperTTS:
    def __init__(self, *, voices_dir: str, model_path: Optional[str] = None, use_cuda: bool = False) -> None:
        self._voices_dir = voices_dir
        self._model_path = model_path or _find_first_voice(voices_dir)
        if not self._model_path:
            raise FileNotFoundError(
                f"No Piper voice model found in {voices_dir}. "
                "Download a .onnx voice file and place it in assets/voices/."
            )
        self._voice = PiperVoice.load(self._model_path, use_cuda=use_cuda)

    def synthesize(self, text: str) -> str:
        if not text:
            raise ValueError("Text must be non-empty.")
        temp_file = tempfile.NamedTemporaryFile(delete=False, suffix=".wav")
        temp_file.close()
        try:
            with wave.open(temp_file.name, "wb") as wav_file:
                self._voice.synthesize_wav(text, wav_file)
            _validate_wav(temp_file.name)
            return temp_file.name
        except (OSError, ValueError, wave.Error) as exc:
            raise TtsError(f"Piper failed to synthesize audio: {exc}") from exc


def _find_first_voice(voices_dir: str) -> Optional[str]:
    if not os.path.isdir(voices_dir):
        return None
    for entry in os.listdir(voices_dir):
        if entry.lower().endswith(".onnx"):
            return os.path.join(voices_dir, entry)
    return None


def _validate_wav(path: str) -> None:
    with wave.open(path, "rb") as wav_file:
        if wav_file.getnchannels() != 1:
            raise ValueError("Piper output must be mono.")
        if wav_file.getsampwidth() != 2:
            raise ValueError("Piper output must be 16-bit PCM.")
        if wav_file.getframerate() != 22050:
            raise ValueError("Piper output must be 22050 Hz for Cozmo playback.")
