from typing import Iterable, Tuple

import numpy as np
from PIL import Image, ImageDraw
import pycozmo

# Canonical names kept (idle/happy/sad/thinking) so pipeline.py, which hardcodes
# "idle" and "thinking", keeps working. The rest are Cartman's moods. "cartman"
# is an alias for the drawn portrait used at idle.
EXPRESSION_NAMES = (
    "idle",
    "happy",
    "sad",
    "thinking",
    "smug",
    "angry",
    "scheming",
    "whatever",
    "pout",
    "authoritah",
    "cartman",
)

# Expressions rendered as a drawn portrait rather than procedural eyes. Both map
# to the same little Cartman face and are produced directly at display size.
_PORTRAIT_EXPRESSIONS = ("idle", "cartman")


def build_face(expression: str) -> pycozmo.procedural_face.ProceduralFace:
    """Build a ProceduralFace (two parametric eyes) for an expression.

    Cartman's moods are sculpted purely through the eyes -- narrowed and lidded
    for smug/scheming, a hard inward V for angry, wide and looking-down for
    authoritah -- because that is what reads on Cozmo's tiny 128x32 display.
    """
    normalized = expression.strip().lower()
    if normalized not in EXPRESSION_NAMES:
        raise ValueError(f"Unknown face expression: {expression}")

    face = pycozmo.procedural_face.ProceduralFace()

    if normalized in ("idle", "cartman"):
        # Idle is drawn as a portrait in render_face_image; if that ever fails
        # it falls back to "smug", so a bare ProceduralFace here is only a
        # last-resort default and is never normally shown.
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

    if normalized == "smug":
        # Narrowed, flat-topped slits looking slightly down the nose at you.
        _apply_lids(face.eyes, top_y=0.30, bottom_y=0.22, top_bend=0.0, bottom_bend=0.0)
        face.center_y = 2.0
        for eye in face.eyes:
            eye.scale_x = 1.0
            eye.scale_y = 0.70
        return face

    if normalized == "angry":
        # Hard inward V-brow: inner corners pulled down toward the nose. This is
        # the mirror of the sad angles, with heavier top lids.
        _apply_lids(face.eyes, top_y=0.40, bottom_y=0.10, top_bend=0.0, bottom_bend=0.0)
        face.eyes[0].angle = 10.0
        face.eyes[1].angle = -10.0
        for eye in face.eyes:
            eye.scale_y = 0.75
        return face

    if normalized == "scheming":
        # Heavy-lidded sidelong plotting glance.
        _apply_lids(face.eyes, top_y=0.45, bottom_y=0.05, top_bend=0.0, bottom_bend=0.0)
        face.center_x = 5.0
        face.center_y = 1.0
        for eye in face.eyes:
            eye.angle = 5.0
            eye.scale_x = 1.0
            eye.scale_y = 0.60
        return face

    if normalized == "whatever":
        # Dismissive eye-roll: gaze up and off to the side.
        _apply_lids(face.eyes, top_y=0.25, bottom_y=0.18, top_bend=0.0, bottom_bend=0.0)
        face.center_x = 4.0
        face.center_y = -5.0
        for eye in face.eyes:
            eye.scale_y = 0.85
        return face

    if normalized == "pout":
        # Pleading puppy eyes: droopy outer corners, big rounded lower lids,
        # looking up.
        _apply_lids(face.eyes, top_y=0.50, bottom_y=0.22, top_bend=0.10, bottom_bend=0.10)
        face.eyes[0].angle = -6.0
        face.eyes[1].angle = 6.0
        face.center_y = -4.0
        for eye in face.eyes:
            eye.scale_x = 1.05
            eye.scale_y = 1.05
        return face

    if normalized == "authoritah":
        # Wide, imposing, looking down on the lowly subject before him.
        _apply_lids(face.eyes, top_y=0.0, bottom_y=0.0, top_bend=0.0, bottom_bend=0.0)
        face.eyes[0].angle = 4.0
        face.eyes[1].angle = -4.0
        face.center_y = 5.0
        for eye in face.eyes:
            eye.scale_x = 1.10
            eye.scale_y = 1.20
        return face

    return face


def render_face_image(expression: str) -> Image.Image:
    """Render an expression to a PIL image sized for Cozmo's display.

    Procedural expressions are rendered by pycozmo at 2x height and then row
    downsampled. The portrait expressions are drawn directly at the final
    display size, so they skip the downsample step.
    """
    normalized = expression.strip().lower()

    if normalized in _PORTRAIT_EXPRESSIONS:
        try:
            return _render_cartman_portrait()
        except Exception:
            # Never let a drawing hiccup crash set_face; fall back to a
            # procedural smug squint, which is still very much in character.
            normalized = "smug"

    face = build_face(normalized)
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


# --- Drawn Cartman portrait ------------------------------------------------

def _base_size_and_mode() -> Tuple[Tuple[int, int], str]:
    """Render a bare ProceduralFace once to learn the display size and mode.

    The procedural path downsamples height by 2 (``np_im[::2]``), so the final
    display size is ``(width, height // 2)``. We draw the portrait at that exact
    size and mode so it swaps in seamlessly with the procedural expressions.
    """
    base = pycozmo.procedural_face.ProceduralFace().render()
    width, height = base.width, base.height
    return (width, max(1, height // 2)), base.mode


def _fg_for_mode(mode: str):
    # Cozmo lights pixels on a dark screen, so the drawing is "on" pixels.
    if mode == "1":
        return 1
    if mode in ("RGB",):
        return (255, 255, 255)
    if mode in ("RGBA",):
        return (255, 255, 255, 255)
    return 255  # L, P, and anything else grayscale-ish


def _bg_for_mode(mode: str):
    if mode == "1":
        return 0
    if mode in ("RGB",):
        return (0, 0, 0)
    if mode in ("RGBA",):
        return (0, 0, 0, 255)
    return 0


def _render_cartman_portrait() -> Image.Image:
    """Draw a tiny, bold Cartman icon: poofball hat over a round, scowling face.

    Everything is line art in the display's foreground colour. Coordinates are
    derived from the canvas size so it scales with whatever pycozmo reports.
    """
    (w, h), mode = _base_size_and_mode()
    fg = _fg_for_mode(mode)
    bg = _bg_for_mode(mode)

    img = Image.new(mode, (w, h), bg)
    d = ImageDraw.Draw(img)

    cx = w / 2.0
    line = max(1, int(round(h / 16.0)))  # stroke weight scales with height

    # Hat geometry.
    hat_half = h * 0.50          # half-width of the beanie
    dome_top = h * 0.04
    brim_y = h * 0.42
    poof_r = max(1.0, h * 0.10)

    # Face geometry.
    face_r = h * 0.46
    face_top = brim_y - h * 0.06
    face_bottom = h * 0.99

    # Round face outline.
    d.ellipse(
        [cx - face_r, face_top, cx + face_r, face_bottom],
        outline=fg,
        width=line,
    )

    # Beanie dome (solid cap) sitting on top of the face, then the brim band.
    d.pieslice(
        [cx - hat_half, dome_top, cx + hat_half, brim_y + h * 0.10],
        start=180,
        end=360,
        fill=fg,
    )
    d.line(
        [cx - hat_half - line, brim_y, cx + hat_half + line, brim_y],
        fill=fg,
        width=max(1, line + 1),
    )

    # Poofball on top.
    d.ellipse(
        [cx - poof_r, dome_top - poof_r, cx + poof_r, dome_top + poof_r],
        fill=fg,
    )

    # Beady eyes.
    eye_y = brim_y + (face_bottom - brim_y) * 0.32
    eye_dx = h * 0.17
    eye_r = max(1.0, h * 0.06)
    for sign in (-1, 1):
        ex = cx + sign * eye_dx
        d.ellipse([ex - eye_r, eye_y - eye_r, ex + eye_r, eye_y + eye_r], fill=fg)

    # Permanent scowl: an upper-arc mouth (corners down).
    mouth_w = h * 0.20
    mouth_h = h * 0.13
    mouth_y = brim_y + (face_bottom - brim_y) * 0.72
    d.arc(
        [cx - mouth_w, mouth_y - mouth_h, cx + mouth_w, mouth_y + mouth_h],
        start=180,
        end=360,
        fill=fg,
        width=line,
    )

    return img