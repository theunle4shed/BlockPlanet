#!/usr/bin/env python3
"""Draws the HUD's heart pictures (BlockPlanet's own art; the owner's design,
2026-11-18: simple heart-shaped containers, white border, red inside, like the
square tiles they replace). Writes assets/ui/hearts/heart.png and
heart-lost.png. Run from the repository root: python3 tools/art/hearts.py
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

SIZE = 128   # drawn at 4x and scaled down, for smooth edges
SCALE = 4


def heart_points(size, inset):
    """The classic heart curve, fitted to a size x size square, `inset` in."""
    points = []
    for i in range(720):
        t = 2 * math.pi * i / 720
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        points.append((x, -y))
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    w, h = max(xs) - min(xs), max(ys) - min(ys)
    room = size - 2 * inset
    k = room / max(w, h)
    ox = inset + (room - w * k) / 2 - min(xs) * k
    oy = inset + (room - h * k) / 2 - min(ys) * k
    return [(ox + x * k, oy + y * k) for x, y in points]


def draw(fill, border, path):
    big = SIZE * SCALE
    image = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(image)
    edge = 0.085 * big  # the border's width, as the tiles' 3dp on 40dp
    d.polygon(heart_points(big, 2 * SCALE), fill=border)
    d.polygon(heart_points(big, 2 * SCALE + edge), fill=fill)
    image.resize((SIZE, SIZE), Image.LANCZOS).save(path)


if __name__ == "__main__":
    out = Path("assets/ui/hearts")
    out.mkdir(parents=True, exist_ok=True)
    draw((0xE0, 0x28, 0x38, 255), (0xF8, 0xF8, 0xF8, 255), out / "heart.png")
    draw((0x20, 0x20, 0x28, 0x80), (0x80, 0x80, 0x80, 255), out / "heart-lost.png")
    print("wrote", out / "heart.png", "and", out / "heart-lost.png")
