from __future__ import annotations

from typing import Optional

import pycozmo
from pycozmo import exception as pycozmo_exception

from . import audio_out, face


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

    def drive(self, speed: float) -> None:
        self._require_connection()
        try:
            self._client.drive_wheels(lwheel_speed=speed, rwheel_speed=speed)
        except pycozmo_exception.PyCozmoException as exc:
            raise CozmoActionError(f"Failed to drive: {exc}") from exc

    def _require_connection(self) -> None:
        if not self._connected or self._client is None:
            raise CozmoConnectionError("Cozmo client is not connected.")
