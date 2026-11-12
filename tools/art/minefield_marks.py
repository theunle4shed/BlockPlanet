#!/usr/bin/env python3
"""Minefield on the sphere: the marks drawn onto the cells (BlockPlanet's own
art). Twelve 32 x 32 tiles in a row: the digits 1 to 8 in the flat
Minefield's colours, then a flag and a mine in white, green and red (the
menus' icon font, Lucide; green marks the mines when the field is won, red
when lost, white the one uncovered), each on a
transparent ground with a dark outline so it reads on the core and on the
cubes. BlockPlanet loads it into the game's free memory and draws a tile over
each cell (src/platform/minefield_sphere.cpp).

    python3 tools/art/minefield_marks.py   # writes assets/ui/minefield/marks.png
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
FONTS = os.path.join(ROOT, "assets", "ui", "fonts")
TILE = 32
COLOURS = ["#50a8f0", "#6ed36a", "#f06060", "#b090f8", "#f2b030", "#3fc8b8", "#f070a8", "#e0e0e0"]
FLAG, MINE = "", ""


def tile(text, font, colour):
    big = 4  # drawn large, then scaled down for smooth edges
    im = Image.new("RGBA", (TILE * big, TILE * big), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    box = d.textbbox((0, 0), text, font=font, stroke_width=3 * big)
    w, h = box[2] - box[0], box[3] - box[1]
    x = (TILE * big - w) / 2 - box[0]
    y = (TILE * big - h) / 2 - box[1]
    d.text((x, y), text, font=font, fill=colour, stroke_width=3 * big, stroke_fill=(10, 10, 14, 235))
    return im.resize((TILE, TILE), Image.LANCZOS)


def main():
    digits = ImageFont.truetype(os.path.join(FONTS, "LatoLatin-Bold.ttf"), 26 * 4)
    icons = ImageFont.truetype(os.path.join(FONTS, "lucide.ttf"), 22 * 4)
    sheet = Image.new("RGBA", (TILE * 12, TILE), (0, 0, 0, 0))
    for n in range(1, 9):
        sheet.paste(tile(str(n), digits, COLOURS[n - 1]), ((n - 1) * TILE, 0))
    sheet.paste(tile(FLAG, icons, "#f5821f"), (8 * TILE, 0))
    sheet.paste(tile(MINE, icons, "#f8f8f8"), (9 * TILE, 0))
    sheet.paste(tile(MINE, icons, "#5bd16a"), (10 * TILE, 0))
    sheet.paste(tile(MINE, icons, "#f05a5a"), (11 * TILE, 0))
    out = os.path.join(ROOT, "assets", "ui", "minefield", "marks.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()
