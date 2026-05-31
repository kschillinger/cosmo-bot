from stt.audio_in import record_seconds
from stt.whisper_stt import WhisperSTT


def main() -> None:
    audio = record_seconds(4.0)
    stt = WhisperSTT(model_name="tiny")
    text = stt.transcribe(audio)
    print(text)


if __name__ == "__main__":
    main()
