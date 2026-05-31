import time
from typing import List, Optional, Tuple

import numpy as np
import pyaudio


class AudioCaptureError(RuntimeError):
    """Raised when microphone capture fails."""


def record_seconds(
    seconds: float,
    *,
    sample_rate: int = 16000,
    channels: int = 1,
    chunk_size: int = 1024,
    device_index: Optional[int] = None,
) -> np.ndarray:
    frames, _ = _record_stream(
        seconds=seconds,
        sample_rate=sample_rate,
        channels=channels,
        chunk_size=chunk_size,
        device_index=device_index,
    )
    return _frames_to_ndarray(frames)


def record_until_silence(
    *,
    sample_rate: int = 16000,
    channels: int = 1,
    chunk_size: int = 1024,
    silence_threshold: float = 500.0,
    silence_duration: float = 1.0,
    max_seconds: float = 10.0,
    device_index: Optional[int] = None,
) -> np.ndarray:
    frames = []
    silence_chunks_needed = int((silence_duration * sample_rate) / chunk_size)
    silent_chunks = 0
    heard_speech = False
    start_time = time.time()

    audio = pyaudio.PyAudio()
    try:
        stream = _open_stream(audio, sample_rate, channels, chunk_size, device_index)
        try:
            while True:
                data = _read_chunk(stream, chunk_size)
                frames.append(data)
                rms = _rms_from_bytes(data)

                if rms >= silence_threshold:
                    heard_speech = True
                    silent_chunks = 0
                elif heard_speech:
                    silent_chunks += 1

                if heard_speech and silent_chunks >= silence_chunks_needed:
                    break
                if time.time() - start_time >= max_seconds:
                    break
        finally:
            stream.stop_stream()
            stream.close()
    finally:
        audio.terminate()

    return _frames_to_ndarray(frames)


def _record_stream(
    *,
    seconds: float,
    sample_rate: int,
    channels: int,
    chunk_size: int,
    device_index: Optional[int],
) -> Tuple[List[bytes], float]:
    frames = []
    audio = pyaudio.PyAudio()
    try:
        stream = _open_stream(audio, sample_rate, channels, chunk_size, device_index)
        try:
            end_time = time.time() + seconds
            while time.time() < end_time:
                frames.append(_read_chunk(stream, chunk_size))
        finally:
            stream.stop_stream()
            stream.close()
    finally:
        audio.terminate()
    return frames, seconds


def _open_stream(
    audio: pyaudio.PyAudio,
    sample_rate: int,
    channels: int,
    chunk_size: int,
    device_index: Optional[int],
) -> pyaudio.Stream:
    try:
        return audio.open(
            format=pyaudio.paInt16,
            channels=channels,
            rate=sample_rate,
            input=True,
            frames_per_buffer=chunk_size,
            input_device_index=device_index,
        )
    except OSError as exc:
        raise AudioCaptureError(f"Failed to open microphone: {exc}") from exc


def _read_chunk(stream: pyaudio.Stream, chunk_size: int) -> bytes:
    try:
        return stream.read(chunk_size, exception_on_overflow=False)
    except OSError as exc:
        raise AudioCaptureError(f"Failed to read microphone data: {exc}") from exc


def _frames_to_ndarray(frames: List[bytes]) -> np.ndarray:
    if not frames:
        return np.array([], dtype=np.int16)
    data = b"".join(frames)
    return np.frombuffer(data, dtype=np.int16)


def _rms_from_bytes(data: bytes) -> float:
    if not data:
        return 0.0
    samples = np.frombuffer(data, dtype=np.int16).astype(np.float32)
    if samples.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(np.square(samples))))
