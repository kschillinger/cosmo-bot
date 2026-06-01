from __future__ import annotations

from typing import Optional

import pycozmo
from pycozmo import exception as pycozmo_exception

from . import audio_out, face, movement


class CozmoConnectionError(RuntimeError):
    """Raised when Cozmo connection fails."""


class CozmoActionError(RuntimeError):
    """Raised when a Cozmo command fails."""


class CozmoClient:
    def __init__(self, *, enable_procedural_face: bool = False) -> None:
        self._client: Optional[pycozmo.Client] = None
        self._connected = False
        self._enable_procedural_face = enable_procedural_face

    def connect(self) -> None:
        if self._connected:
            return
        try:
            self._client = pycozmo.Client(enable_procedural_face=self._enable_procedural_face)
            self._client.start()
            self._client.connect()
            self._client.wait_for_robot()
            self._connected = True
        except (pycozmo_exception.PyCozmoConnectionError, OSError) as exc:
            raise CozmoConnectionError(
                "Failed to connect to Cozmo. Ensure you are on the Cozmo Wi-Fi network."
            ) from exc

    def disconnect(self) -> None:
        if not self._client:
            return
        try:
            self._client.disconnect()
            self._client.stop()
        finally:
            self._connected = False
            self._client = None

    def set_face(self, expression: str) -> None:
        self._require_connection()
        try:
            image = face.render_face_image(expression)
            self._client.display_image(image)
        except (pycozmo_exception.PyCozmoException, ValueError) as exc:
            raise CozmoActionError(f"Failed to set face: {exc}") from exc

    def play_wav(self, path: str) -> None:
        self._require_connection()
        try:
            audio_out.play_wav(self._client, path)
        except audio_out.AudioOutputError as exc:
            raise CozmoActionError(str(exc)) from exc

    # --- Movement -----------------------------------------------------------
    # Thin wrappers over the movement module. Each one translates a
    # MovementError into a CozmoActionError so callers only have to handle one
    # action-error type, consistent with set_face and play_wav above.

    def set_head_angle(self, degrees: float) -> None:
        """Tilt the head to an absolute angle in degrees (-25 down .. 44.5 up)."""
        self._require_connection()
        try:
            movement.set_head_angle(self._client, degrees)
        except movement.MovementError as exc:
            raise CozmoActionError(str(exc)) from exc

    def set_lift(self, ratio: float) -> None:
        """Raise or lower the arms. ratio 0.0 = fully down, 1.0 = fully up."""
        self._require_connection()
        try:
            movement.set_lift_ratio(self._client, ratio)
        except movement.MovementError as exc:
            raise CozmoActionError(str(exc)) from exc

    def drive(
        self,
        left_mmps: float,
        right_mmps: Optional[float] = None,
        *,
        duration: Optional[float] = None,
    ) -> None:
        """Drive the treads.

        Pass a single speed to go straight, or distinct left/right speeds to
        curve or turn. With ``duration`` set, drive for that many seconds and
        then stop; otherwise keep going until ``stop`` is called.
        """
        self._require_connection()
        if right_mmps is None:
            right_mmps = left_mmps
        try:
            movement.drive_wheels(self._client, left_mmps, right_mmps, duration=duration)
        except movement.MovementError as exc:
            raise CozmoActionError(str(exc)) from exc

    def turn(self, degrees: float, *, speed_mmps: float = 50.0) -> None:
        """Turn in place. Positive degrees turns left (counter-clockwise)."""
        self._require_connection()
        try:
            movement.turn_in_place(self._client, degrees, speed_mmps=speed_mmps)
        except movement.MovementError as exc:
            raise CozmoActionError(str(exc)) from exc

    def stop(self) -> None:
        """Stop all motors at once (wheels, head, and lift)."""
        self._require_connection()
        try:
            movement.stop(self._client)
        except movement.MovementError as exc:
            raise CozmoActionError(str(exc)) from exc

    def nod(self) -> None:
        """Nod the head up and down (a friendly 'yes')."""
        self._require_connection()
        try:
            movement.nod(self._client)
        except movement.MovementError as exc:
            raise CozmoActionError(str(exc)) from exc

    def wave_arms(self) -> None:
        """Wave the lift arms up and down."""
        self._require_connection()
        try:
            movement.wave_arms(self._client)
        except movement.MovementError as exc:
            raise CozmoActionError(str(exc)) from exc

    def wiggle(self) -> None:
        """Do an excited side-to-side shimmy in place."""
        self._require_connection()
        try:
            movement.wiggle(self._client)
        except movement.MovementError as exc:
            raise CozmoActionError(str(exc)) from exc

    def _require_connection(self) -> None:
        if not self._connected or self._client is None:
            raise CozmoConnectionError("Cozmo client is not connected.")
