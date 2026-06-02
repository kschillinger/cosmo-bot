"""Movement control for Cozmo: head tilt, lift (arms), and wheels (treads).

Every function takes a live ``pycozmo.Client`` as its first argument, mirroring
the ``audio_out`` module. Callers pass friendly units -- degrees for the head, a
0.0-1.0 ratio for the lift, and mm/s for the wheels -- and inputs are clamped to
Cozmo's documented physical limits so an out-of-range command can never be sent.

Limits (from pycozmo.robot):
    head angle  : -25.0 to 44.5 degrees
    lift height : 32.0 to 92.0 mm  (exposed here as a 0.0-1.0 ratio)
    wheel speed : -200.0 to 200.0 mm/s
    track width : 45.0 mm  (used to convert a turn angle into a drive duration)

Timed wheel moves are implemented locally (drive, then sleep, then stop) rather
than relying on pycozmo's optional ``duration`` argument, so the stop is always
explicit and deterministic.
"""

from __future__ import annotations

import math
import time
from typing import Optional

import pycozmo
from pycozmo import exception as pycozmo_exception


class MovementError(RuntimeError):
    """Raised when a Cozmo movement command fails."""


# Physical limits, taken from pycozmo.robot constants.
HEAD_ANGLE_MIN_DEG = -25.0
HEAD_ANGLE_MAX_DEG = 44.5
LIFT_HEIGHT_MIN_MM = 32.0
LIFT_HEIGHT_MAX_MM = 92.0
WHEEL_SPEED_MAX_MMPS = 200.0
TRACK_WIDTH_MM = 45.0


def _require_client(client: Optional[pycozmo.Client]) -> None:
    if client is None:
        raise MovementError("Cozmo client is not connected.")


def _clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


# --- Head ------------------------------------------------------------------

def set_head_angle(client: pycozmo.Client, degrees: float, *, duration: float = 0.0) -> None:
    """Tilt the head to an absolute angle in degrees.

    -25.0 looks all the way down, 0.0 is roughly level, 44.5 looks all the way
    up. Values outside that range are clamped.
    """
    _require_client(client)
    angle_deg = _clamp(degrees, HEAD_ANGLE_MIN_DEG, HEAD_ANGLE_MAX_DEG)
    try:
        client.set_head_angle(angle=math.radians(angle_deg), duration=duration)
    except pycozmo_exception.PyCozmoException as exc:
        raise MovementError(f"Failed to set head angle: {exc}") from exc


# --- Lift (arms) -----------------------------------------------------------

def set_lift_ratio(client: pycozmo.Client, ratio: float, *, duration: float = 0.0) -> None:
    """Raise or lower the lift arms. ratio 0.0 = fully down, 1.0 = fully up."""
    _require_client(client)
    ratio = _clamp(ratio, 0.0, 1.0)
    height_mm = LIFT_HEIGHT_MIN_MM + ratio * (LIFT_HEIGHT_MAX_MM - LIFT_HEIGHT_MIN_MM)
    try:
        client.set_lift_height(height=height_mm, duration=duration)
    except pycozmo_exception.PyCozmoException as exc:
        raise MovementError(f"Failed to set lift height: {exc}") from exc


# --- Wheels (treads) -------------------------------------------------------

def drive_wheels(
    client: pycozmo.Client,
    left_mmps: float,
    right_mmps: float,
    *,
    duration: Optional[float] = None,
) -> None:
    """Drive the treads at independent left/right speeds in mm/s.

    With ``duration`` set, this blocks for that many seconds and then stops the
    motors. With ``duration`` None it returns immediately and the treads keep
    moving until ``stop`` is called.
    """
    _require_client(client)
    left = _clamp(left_mmps, -WHEEL_SPEED_MAX_MMPS, WHEEL_SPEED_MAX_MMPS)
    right = _clamp(right_mmps, -WHEEL_SPEED_MAX_MMPS, WHEEL_SPEED_MAX_MMPS)
    try:
        client.drive_wheels(lwheel_speed=left, rwheel_speed=right)
        if duration is not None:
            time.sleep(max(0.0, duration))
            client.stop_all_motors()
    except pycozmo_exception.PyCozmoException as exc:
        raise MovementError(f"Failed to drive wheels: {exc}") from exc


def drive_straight(client: pycozmo.Client, speed_mmps: float = 50.0, *, duration: float = 1.0) -> None:
    """Drive in a straight line for ``duration`` seconds. Negative drives back."""
    drive_wheels(client, speed_mmps, speed_mmps, duration=duration)


def turn_in_place(client: pycozmo.Client, degrees: float, *, speed_mmps: float = 50.0) -> None:
    """Rotate the body in place. Positive degrees turns left (counter-clockwise).

    Duration is derived from differential-drive kinematics
    (angular speed = 2 * wheel_speed / track_width). Real turns are approximate
    because the treads slip, so treat the angle as a target, not a guarantee.
    """
    _require_client(client)
    speed = _clamp(abs(speed_mmps), 1.0, WHEEL_SPEED_MAX_MMPS)
    angular_speed = (2.0 * speed) / TRACK_WIDTH_MM  # rad/s
    duration = abs(math.radians(degrees)) / angular_speed
    if degrees >= 0.0:
        drive_wheels(client, -speed, speed, duration=duration)
    else:
        drive_wheels(client, speed, -speed, duration=duration)


def stop(client: pycozmo.Client) -> None:
    """Stop all motors at once (wheels, head, and lift)."""
    _require_client(client)
    try:
        client.stop_all_motors()
    except pycozmo_exception.PyCozmoException as exc:
        raise MovementError(f"Failed to stop motors: {exc}") from exc


# --- Expressive gestures ---------------------------------------------------

def nod(client: pycozmo.Client, *, times: int = 2) -> None:
    """A friendly 'yes' nod: tilt the head down and up a few times."""
    _require_client(client)
    for _ in range(max(1, times)):
        set_head_angle(client, HEAD_ANGLE_MIN_DEG + 5.0)
        time.sleep(0.25)
        set_head_angle(client, HEAD_ANGLE_MAX_DEG - 10.0)
        time.sleep(0.25)
    set_head_angle(client, 0.0)


def wave_arms(client: pycozmo.Client, *, times: int = 2) -> None:
    """Raise and lower the lift a few times, like an excited little wave."""
    _require_client(client)
    for _ in range(max(1, times)):
        set_lift_ratio(client, 1.0)
        time.sleep(0.3)
        set_lift_ratio(client, 0.0)
        time.sleep(0.3)


def wiggle(client: pycozmo.Client, *, times: int = 2, speed_mmps: float = 80.0) -> None:
    """An excited shimmy: quick left-right turns in place."""
    _require_client(client)
    for _ in range(max(1, times)):
        turn_in_place(client, 20.0, speed_mmps=speed_mmps)
        turn_in_place(client, -20.0, speed_mmps=speed_mmps)
    stop(client)
