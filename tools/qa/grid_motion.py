#!/usr/bin/env python3
"""Measure how the tiles of a menu grid move, from a recording's frames.

For the menu grid's idle motion (docs/UI.md, Menu grid): the owner finds the
original's tiles "more independent" than BlockPlanet's travelling wave, and
asked for a way to analyse it. Record the original's moving menu background
(no panels over it, at least 30 frames a second, 10 seconds or more), save
the frames as numbered PNG files, and give the rectangle the tiles fill and
how many columns and rows there are:

    python3 tools/qa/grid_motion.py FRAMES_DIR --fps 30 --box X0 Y0 X1 Y1 --grid 9 7

For each tile it follows three signals over time, inside the tile's cell:
its brightness (a tile coming forward or tilting catches the light
differently), its shift (where its pattern moves, by phase correlation with
the first frame) and its shading slope (left-right and top-bottom brightness
differences: its tilt). For each tile and signal it finds the main period,
how strong the motion is, and its phase. Then, across the grid:

- period: the typical period, and how much tiles differ (speed variation);
- wave: whether phases line up as one wave travelling across the grid (a
  plane fitted to the phases by column and row: its direction, and the delay
  from one tile to the next);
- independence: how much of the phase is NOT explained by that wave (0: a
  perfect travelling wave, 1: every tile on its own), and how alike
  neighbouring tiles move (their correlation);
- tilt: whether tiles tilt as well as rise (the shading slope's strength).

It ends with settings to try on the grid test screen (Options > Grid test).
Frames stay out of Git (AGENTS.md: captures are private).

`--selftest` makes frames of a known motion and checks the tool reads it back.
"""

import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def load_frames(folder, box):
    paths = sorted(p for p in Path(folder).iterdir() if p.suffix.lower() == ".png")
    if len(paths) < 16:
        sys.exit("need at least 16 PNG frames")
    frames = []
    for p in paths:
        im = Image.open(p).convert("L")
        if box:
            im = im.crop(tuple(box))
        frames.append(np.asarray(im, dtype=np.float32) / 255.0)
    return np.stack(frames)


def shift(a, b):
    """Sub-pixel shift of b against a, by phase correlation."""
    fa, fb = np.fft.fft2(a - a.mean()), np.fft.fft2(b - b.mean())
    r = fa * np.conj(fb)
    r /= np.abs(r) + 1e-9
    c = np.fft.ifft2(r).real
    y, x = np.unravel_index(np.argmax(c), c.shape)
    h, w = c.shape

    def refine(v, n, line):
        l, m, rr = line[(v - 1) % n], line[v], line[(v + 1) % n]
        d = (l - 2 * m + rr)
        off = 0.5 * (l - rr) / d if abs(d) > 1e-9 else 0.0
        v = v + off
        return v - n if v > n / 2 else v

    return refine(x, w, c[y, :]), refine(y, h, c[:, x])


def tile_signals(frames, columns, rows):
    n, height, width = frames.shape
    cw, ch = width / columns, height / rows
    signals = {}
    for r in range(rows):
        for c in range(columns):
            # The middle of the cell (away from the gaps and neighbours).
            x0, x1 = int(c * cw + cw * 0.2), int((c + 1) * cw - cw * 0.2)
            y0, y1 = int(r * ch + ch * 0.2), int((r + 1) * ch - ch * 0.2)
            cell = frames[:, y0:y1, x0:x1]
            bright = cell.mean(axis=(1, 2))
            half_w, half_h = cell.shape[2] // 2, cell.shape[1] // 2
            slope_x = cell[:, :, half_w:].mean(axis=(1, 2)) - cell[:, :, :half_w].mean(axis=(1, 2))
            slope_y = cell[:, half_h:, :].mean(axis=(1, 2)) - cell[:, :half_h, :].mean(axis=(1, 2))
            moves = np.array([shift(cell[0], cell[i]) for i in range(n)])
            signals[(c, r)] = {
                "brightness": bright,
                "shift_x": moves[:, 0],
                "shift_y": moves[:, 1],
                "slope_x": slope_x,
                "slope_y": slope_y,
            }
    return signals


def main_tone(signal, fps):
    """Main period (s), strength (standard deviation) and phase (radians)."""
    s = signal - np.polyval(np.polyfit(np.arange(len(signal)), signal, 1), np.arange(len(signal)))
    n = len(s)
    window = np.hanning(n)
    spectrum = np.fft.rfft(s * window)
    freqs = np.fft.rfftfreq(n, 1.0 / fps)
    spectrum[0] = 0
    if n > 8:
        spectrum[freqs < 2.0 * fps / n] = 0  # below two cycles in the recording: not measurable
    k = int(np.argmax(np.abs(spectrum)))
    if k == 0 or freqs[k] <= 0:
        return None
    # Refine the peak (parabola on the magnitudes).
    mags = np.abs(spectrum)
    if 0 < k < len(mags) - 1:
        l, m, r = mags[k - 1], mags[k], mags[k + 1]
        d = l - 2 * m + r
        off = 0.5 * (l - r) / d if abs(d) > 1e-12 else 0.0
    else:
        off = 0.0
    freq = (k + off) * fps / n
    # Phase by fitting a sine at that frequency.
    t = np.arange(n) / fps
    basis = np.stack([np.sin(2 * math.pi * freq * t), np.cos(2 * math.pi * freq * t)], axis=1)
    (a, b), *_ = np.linalg.lstsq(basis, s, rcond=None)
    return {"period": 1.0 / freq, "strength": float(np.std(s)), "phase": math.atan2(b, a)}


def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


def analyse(frames, fps, columns, rows, signal_name=None):
    signals = tile_signals(frames, columns, rows)
    names = ["brightness", "shift_x", "shift_y", "slope_x", "slope_y"]
    # The signal that moves most (relative to its own noise) speaks for the motion.
    if signal_name is None:
        def score(name):
            vals = [main_tone(sig[name], fps) for sig in signals.values()]
            vals = [v for v in vals if v]
            return np.median([v["strength"] for v in vals]) if vals else 0.0
        strengths = {name: score(name) for name in names}
        # Shifts are in pixels, the others in brightness: compare each to its own spread.
        normalised = {name: strengths[name] / (np.median([np.std(sig[name]) for sig in signals.values()]) + 1e-9)
                      for name in names}
        signal_name = max(names, key=lambda n: (strengths[n] > 0, normalised[n] * (1 if n.startswith("shift") else 1)))
    tones = {}
    for key, sig in signals.items():
        tone = main_tone(sig[signal_name], fps)
        if tone:
            tones[key] = tone
    if len(tones) < 4:
        sys.exit("too few tiles move measurably")
    periods = np.array([t["period"] for t in tones.values()])
    period = float(np.median(periods))
    spread = float(np.median(np.abs(periods - period)) / period)
    # Phases unwrapped against the median period's frequency: fit a plane
    # phase = p0 - (gu * column + gv * row), a wave travelling along (gu, gv).
    keys = list(tones)
    cols = np.array([k[0] for k in keys], dtype=float)
    rws = np.array([k[1] for k in keys], dtype=float)
    phases = np.array([tones[k]["phase"] for k in keys])
    best = None
    for gu in np.linspace(-math.pi, math.pi, 73):
        for gv in np.linspace(-math.pi, math.pi, 73):
            resid = np.array([wrap(p + gu * c + gv * r) for p, c, r in zip(phases, cols, rws)])
            p0 = math.atan2(np.sin(resid).mean(), np.cos(resid).mean())
            err = np.array([wrap(x - p0) for x in resid])
            cost = float(np.mean(err ** 2))
            if best is None or cost < best[0]:
                best = (cost, gu, gv, p0, err)
    _, gu, gv, _, err = best
    # Phase scatter left over: 0 for a clean travelling wave; random phases
    # give about 1.8 radians (uniform), so scale to 0..1.
    scatter = float(np.sqrt(np.mean(err ** 2)))
    independence = min(1.0, scatter / 1.81)
    step_phase = math.hypot(gu, gv)  # radians per tile along the wave
    tile_delay = step_phase / (2 * math.pi) * period
    direction = math.degrees(math.atan2(gv, gu))  # 0: travels right, 90: down
    # Neighbours alike? Mean correlation of right and down neighbours.
    corr = []
    for (c, r), sig in signals.items():
        for nb in ((c + 1, r), (c, r + 1)):
            if nb in signals:
                a, b = sig[signal_name], signals[nb][signal_name]
                if np.std(a) > 1e-9 and np.std(b) > 1e-9:
                    corr.append(float(np.corrcoef(a, b)[0, 1]))
    tilt = float(np.median([max(np.std(s["slope_x"]), np.std(s["slope_y"])) for s in signals.values()]))
    rise = float(np.median([np.std(s["brightness"]) for s in signals.values()]))
    result = {
        "signal": signal_name,
        "tiles_measured": len(tones),
        "period_s": round(period, 3),
        "period_spread": round(spread, 3),
        "wave_direction_deg": round(direction, 1),
        "tile_delay_s": round(tile_delay, 3),
        "phase_scatter_rad": round(scatter, 3),
        "independence": round(independence, 2),
        "neighbour_correlation": round(float(np.mean(corr)) if corr else 0.0, 2),
        "tilt_vs_rise": round(tilt / (rise + 1e-9), 2),
        "suggested_test_screen": {
            "Wave period (s)": round(period, 2),
            "Wave tile delay (ms)": int(round(tile_delay * 1000 / 10.0) * 10),
            "Independence (%)": int(round(independence * 10) * 10),
            "Speed variation (%)": int(round(min(0.5, spread * 2.0) * 100 / 5.0) * 5),
        },
    }
    return result


def selftest():
    """Frames of a known motion: tiles bobbing (brightness and a small
    vertical shift) on a wave from the left, delay 0.1 s per tile, 1.5 s
    period, and some tiles out of step. The tool must read the period and
    delay back, and see more independence when phases are scrambled."""
    rng = np.random.default_rng(1)
    fps, seconds, columns, rows, cell = 30, 9, 6, 4, 40
    texture = rng.random((cell * 2, cell * 2)).astype(np.float32)
    texture = (texture + np.roll(texture, 1, 0) + np.roll(texture, 1, 1)) / 3

    def make(scramble):
        offsets = rng.uniform(-math.pi, math.pi, (columns, rows)) if scramble else np.zeros((columns, rows))
        frames = []
        for i in range(fps * seconds):
            t = i / fps
            im = np.zeros((rows * cell, columns * cell), dtype=np.float32)
            for c in range(columns):
                for r in range(rows):
                    ph = 2 * math.pi * (t / 1.5) - 2 * math.pi * (c * 0.1) / 1.5 + offsets[c, r]
                    dy = 2.0 * math.sin(ph)
                    y = int(round(cell / 2 + dy))
                    patch = texture[y:y + cell, cell // 2:cell // 2 + cell] * (0.6 + 0.2 * math.sin(ph))
                    im[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell] = patch
            frames.append(im)
        return np.stack(frames)

    clean = analyse(make(False), fps, columns, rows)
    messy = analyse(make(True), fps, columns, rows)
    print(json.dumps({"clean": clean, "scrambled": messy}, indent=2))
    assert abs(clean["period_s"] - 1.5) < 0.1, clean
    assert abs(clean["tile_delay_s"] - 0.1) < 0.03, clean
    assert abs(abs(clean["wave_direction_deg"]) - 0) < 15, clean  # travelling right
    assert clean["independence"] < 0.2 and messy["independence"] > 0.5, (clean, messy)
    print("selftest passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("frames", nargs="?", help="folder of numbered PNG frames")
    parser.add_argument("--fps", type=float, default=30.0)
    parser.add_argument("--box", type=int, nargs=4, metavar=("X0", "Y0", "X1", "Y1"),
                        help="the rectangle the tiles fill (default: the whole frame)")
    parser.add_argument("--grid", type=int, nargs=2, metavar=("COLUMNS", "ROWS"), default=[9, 7])
    parser.add_argument("--signal", choices=["brightness", "shift_x", "shift_y", "slope_x", "slope_y"],
                        help="which signal to follow (default: the one that moves most)")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return
    if not args.frames:
        parser.error("give a frames folder, or --selftest")
    frames = load_frames(args.frames, args.box)
    print(json.dumps(analyse(frames, args.fps, args.grid[0], args.grid[1], args.signal), indent=2))


if __name__ == "__main__":
    main()
