#!/usr/bin/env python3
"""Minefield on the sphere: the numbers on the uncovered core (BlockPlanet's
own art). Nine 16 x 16 core tiles in a row: a plain tile (no mines round the
cell), then the digits 1 to 8 in the flat Minefield's colours with a dark
outline, on the same pale ground. A second row holds the same nine tiles
highlighted (a warm ground and a gold frame) for the cell under the cursor.
The image keeps to 255 colours, as the core's tiles are drawn through a
256-colour palette.

BlockPlanet writes these tiles and their palette over the core's picture tiles
(the ones Hide & Seek's pictures use, values 72 to 80) while the field is in
play, so the game draws the numbers as part of the core itself
(src/platform/minefield_sphere.cpp).

    python3 tools/art/minefield_marks.py   # writes assets/ui/minefield/core_digits.png
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
FONTS = os.path.join(ROOT, "assets", "ui", "fonts")
TILE = 16
BIG = 8  # drawn large, then scaled down for smooth edges
GROUND = (236, 238, 244)
EDGE = (200, 204, 216)
CURSOR_GROUND = (255, 236, 160)
CURSOR_EDGE = (232, 150, 0)
COLOURS = ["#2f86e0", "#2fa84a", "#e04646", "#7a52d8", "#d88a10", "#1aa596", "#d84a92", "#4a4a56"]


def tile(text, font, colour, cursor=False):
    size = TILE * BIG
    im = Image.new("RGB", (size, size), CURSOR_GROUND if cursor else GROUND)
    d = ImageDraw.Draw(im)
    # A faint edge, so neighbouring cells read as separate squares; under the
    # cursor, a gold frame.
    d.rectangle((0, 0, size - 1, size - 1), outline=CURSOR_EDGE if cursor else EDGE,
                width=BIG * 2 if cursor else BIG // 2)
    if text:
        stroke = BIG // 2 + 1
        box = d.textbbox((0, 0), text, font=font, stroke_width=stroke)
        w, h = box[2] - box[0], box[3] - box[1]
        x = (size - w) / 2 - box[0]
        y = (size - h) / 2 - box[1]
        d.text((x, y), text, font=font, fill=colour, stroke_width=stroke, stroke_fill=(16, 16, 22))
    return im.resize((TILE, TILE), Image.LANCZOS)


def main():
    digits = ImageFont.truetype(os.path.join(FONTS, "LatoLatin-Bold.ttf"), 14 * BIG)
    sheet = Image.new("RGB", (TILE * 9, TILE * 2), GROUND)
    for row, cursor in enumerate((False, True)):
        sheet.paste(tile("", digits, None, cursor), (0, row * TILE))
        for n in range(1, 9):
            sheet.paste(tile(str(n), digits, COLOURS[n - 1], cursor), (n * TILE, row * TILE))
    sheet = sheet.quantize(colors=255, method=Image.Quantize.MEDIANCUT).convert("RGB")
    out = os.path.join(ROOT, "assets", "ui", "minefield", "core_digits.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()
