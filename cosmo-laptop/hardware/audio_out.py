import math
import os
import struct
import wave
from typing import Optional

import pycozmo
from pycozmo import exception as pycozmo_exception


class AudioOutputError(RuntimeError):
    """Raised when Cozmo audio playback fails."""


def play_wav(client: pycozmo.Client, path: str) -> None:
    if client is None:
        raise AudioOutputError("Cozmo client is not connected.")
    try:
        client.play_audio(path)
    except (pycozmo_exception.PyCozmoException, OSError, ValueError) as exc:
        raise AudioOutputError(f"Failed to play audio: {exc}") from exc


def ensure_test_beep(path: str) -> None:
    if _wav_is_valid(path):
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    _write_sine_wav(path, duration_s=0.25, freq_hz=880.0, sample_rate=22050, amplitude=0.4)


def _wav_is_valid(path: str) -> bool:
    if not os.path.exists(path):
        return False
    if os.path.getsize(path) < 44:
        return False
    try:
        with wave.open(path, "rb") as wav_file:
            if wav_file.getsampwidth() != 2:
                return False
            if wav_file.getnchannels() != 1:
                return False
            if wav_file.getframerate() not in (22050, 48000):
                return False
    except (wave.Error, EOFError):
        return False
    return True


def _write_sine_wav(
    path: str,
    *,
    duration_s: float,
    freq_hz: float,
    sample_rate: int,
    amplitude: float,
) -> None:
    total_samples = int(sample_rate * duration_s)
    with wave.open(path, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        frames = bytearray()
        for i in range(total_samples):
            value = amplitude * math.sin(2.0 * math.pi * freq_hz * (i / sample_rate))
            sample = max(-1.0, min(1.0, value))
            frames.extend(struct.pack("<h", int(sample * 32767)))
        wav_file.writeframes(frames)
