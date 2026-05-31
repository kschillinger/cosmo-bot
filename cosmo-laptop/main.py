import os
import time

from hardware import audio_out
from hardware.cozmo_client import CozmoActionError, CozmoClient, CozmoConnectionError


def main() -> None:
    client = CozmoClient()
    assets_dir = os.path.join(os.path.dirname(__file__), "assets")
    wav_path = os.path.join(assets_dir, "test_beep.wav")

    try:
        client.connect()
        audio_out.ensure_test_beep(wav_path)
        client.set_face("happy")
        time.sleep(2.0)
        client.play_wav(wav_path)
        client.set_face("idle")
    except CozmoConnectionError as exc:
        print(f"[Cozmo] Connection failed: {exc}")
    except CozmoActionError as exc:
        print(f"[Cozmo] Action failed: {exc}")
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
