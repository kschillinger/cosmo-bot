import time

from hardware.cozmo_client import CozmoActionError, CozmoClient, CozmoConnectionError


def main() -> None:
    client = CozmoClient()
    try:
        client.connect()

        print("[Movement] Head: up, level, down, level")
        client.set_head_angle(40.0)
        time.sleep(1.0)
        client.set_head_angle(0.0)
        time.sleep(1.0)
        client.set_head_angle(-20.0)
        time.sleep(1.0)
        client.set_head_angle(0.0)
        time.sleep(0.5)

        print("[Movement] Arms: raise, lower")
        client.set_lift(1.0)
        time.sleep(1.0)
        client.set_lift(0.0)
        time.sleep(1.0)

        print("[Movement] Wheels: forward, back, turn left, turn right")
        client.drive(60.0, duration=1.0)
        time.sleep(0.3)
        client.drive(-60.0, duration=1.0)
        time.sleep(0.3)
        client.turn(90.0)
        time.sleep(0.3)
        client.turn(-90.0)
        time.sleep(0.5)

        print("[Movement] Gestures: nod, wave arms, wiggle")
        client.nod()
        client.wave_arms()
        client.wiggle()

        client.stop()
        print("[Movement] Done.")
    except CozmoConnectionError as exc:
        print(f"[Cozmo] Connection failed: {exc}")
    except CozmoActionError as exc:
        print(f"[Cozmo] Movement failed: {exc}")
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
