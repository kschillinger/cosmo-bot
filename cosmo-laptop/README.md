# Cosmo Hardware — Laptop Pipeline

This folder contains the offline laptop-side pipeline for controlling a real Anki Cozmo over Wi‑Fi using PyCozmo.

## Setup

1. Power on Cozmo and wait for the Wi‑Fi network (e.g., `Cozmo_XXXXXX`), then connect your laptop to it.
2. Create a virtual environment and install dependencies:
   ```sh
   python -m venv .venv
   .venv\Scripts\activate
   pip install -r requirements.txt
   ```
3. Download a Piper voice model and place the `.onnx` (and matching `.json`) in `assets\voices\`:
   ```sh
   python -m piper.download_voices en_US-lessac-medium
   ```
   Copy the downloaded files from the Piper cache into `assets\voices\`.

## Smoke tests

- Phase 1 hardware test:
  ```sh
  python main.py
  ```
  This shows a happy face for 2 seconds, plays `assets\test_beep.wav`, then idles. If the file is missing or invalid,
  it is regenerated automatically.

- Phase 2 STT test:
  ```sh
  python test_stt.py
  ```

- Phase 3 dialogue test:
  ```sh
  python test_dialogue.py
  ```

- Phase 4 TTS test:
  ```sh
  python test_tts.py
  ```

## Full pipeline

```sh
python pipeline.py
```

The loop runs: record mic audio → transcribe → classify → respond → synthesize → play through Cozmo and animate the face.

## Notes

- Fully offline: no cloud APIs are used at runtime.
- PyAudio may require PortAudio on Windows; install a prebuilt wheel if needed.
