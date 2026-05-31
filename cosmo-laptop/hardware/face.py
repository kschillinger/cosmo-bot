from typing import Iterable

import numpy as np
from PIL import Image
import pycozmo

EXPRESSION_NAMES = ("idle", "happy", "sad", "thinking")


def build_face(expression: str) -> pycozmo.procedural_face.ProceduralFace:
    normalized = expression.strip().lower()
    if normalized not in EXPRESSION_NAMES:
        raise ValueError(f"Unknown face expression: {expression}")

    face = pycozmo.procedural_face.ProceduralFace()

    if normalized == "idle":
        return face

    if normalized == "happy":
        _apply_lids(face.eyes, top_y=0.08, bottom_y=0.02, top_bend=0.15, bottom_bend=0.05)
        for eye in face.eyes:
            eye.scale_x = 1.05
            eye.scale_y = 1.10
        return face

    if normalized == "sad":
        _apply_lids(face.eyes, top_y=0.55, bottom_y=0.12, top_bend=0.05, bottom_bend=0.0)
        face.eyes[0].angle = -8.0
        face.eyes[1].angle = 8.0
        for eye in face.eyes:
            eye.scale_y = 0.90
        return face

    if normalized == "thinking":
        _apply_lids(face.eyes, top_y=0.35, bottom_y=0.20, top_bend=0.10, bottom_bend=0.0)
        face.center_x = -6.0
        face.center_y = -4.0
        for eye in face.eyes:
            eye.scale_x = 0.95
            eye.scale_y = 0.85
        return face

    return face


def render_face_image(expression: str) -> Image.Image:
    face = build_face(expression)
    im = face.render()
    np_im = np.array(im)
    np_im2 = np_im[::2]
    return Image.fromarray(np_im2)


def _apply_lids(
    eyes: Iterable[pycozmo.procedural_face.ProceduralEye],
    *,
    top_y: float,
    bottom_y: float,
    top_bend: float,
    bottom_bend: float,
) -> None:
    for eye in eyes:
        eye.lids[0].y = top_y
        eye.lids[0].bend = top_bend
        eye.lids[1].y = bottom_y
        eye.lids[1].bend = bottom_bend
