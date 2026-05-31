from __future__ import annotations

from typing import Optional, Union

import numpy as np
import whisper


class SttError(RuntimeError):
    """Raised when Whisper transcription fails."""


class WhisperSTT:
    def __init__(self, model_name: str = "tiny") -> None:
        self._model = whisper.load_model(model_name)
        self._decode_options = whisper.DecodingOptions(fp16=False)

    def transcribe(self, audio: Union[str, np.ndarray]) -> str:
        try:
            samples = self._load_audio(audio)
            if samples.size == 0:
                return ""
            samples = whisper.pad_or_trim(samples)
            mel = whisper.log_mel_spectrogram(samples).to(self._model.device)
            result = whisper.decode(self._model, mel, self._decode_options)
            return result.text.strip()
        except (ValueError, RuntimeError, OSError) as exc:
            raise SttError(f"Whisper failed to transcribe: {exc}") from exc

    def _load_audio(self, audio: Union[str, np.ndarray]) -> np.ndarray:
        if isinstance(audio, str):
            samples = whisper.load_audio(audio)
        elif isinstance(audio, np.ndarray):
            samples = audio
        else:
            raise ValueError("Audio must be a file path or numpy array.")

        if samples.ndim > 1:
            samples = np.mean(samples, axis=1)

        if samples.dtype != np.float32:
            samples = samples.astype(np.float32)
        if samples.size and np.max(np.abs(samples)) > 1.0:
            samples /= 32768.0

        return samples
