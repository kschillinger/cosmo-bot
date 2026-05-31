import os
import shutil

from tts.piper_tts import PiperTTS


def main() -> None:
    root_dir = os.path.dirname(__file__)
    assets_dir = os.path.join(root_dir, "assets")
    voices_dir = os.path.join(assets_dir, "voices")
    os.makedirs(assets_dir, exist_ok=True)

    tts = PiperTTS(voices_dir=voices_dir)
    temp_path = tts.synthesize("Hello, I am Cozmo")
    output_path = os.path.join(assets_dir, "test_output.wav")
    shutil.copyfile(temp_path, output_path)
    os.remove(temp_path)
    print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()
