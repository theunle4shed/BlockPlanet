#!/usr/bin/env python3
"""Print Tetrisphere's level parameter table from the player's own ROM.

The output is derived from original game data, so write it to a private
location (for example .local/) and do not commit it. The format is described
in docs/research/blockplanet-level-data.md.

Usage:
    python3 -m tools.analysis.level_table ROM.z64 [--csv] [--all]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ROM_MANIFEST = ROOT / "config/roms/tetrisphere-us-rev0.json"

TABLE = 0x7D4A64          # offset table: (u16 offset, u16 0) entries
ENTRIES = 347
RECORD_BASE = 0x7D4FD0 - 0x55E   # entry offsets are relative to this
RESCUE_FIRST = 4          # entry index of chapter 1, level 1
RESCUE_CHAPTERS = 15
LEVELS_PER_CHAPTER = 10
FIRST_COLOUR_TYPE = 32    # colour mask bit k = block type 32 + k


def load_rom(path: Path) -> bytes:
    data = path.read_bytes()
    manifest = json.loads(ROM_MANIFEST.read_text())
    if hashlib.sha256(data).hexdigest() != manifest["sha256"]:
        raise SystemExit("ROM is not the supported NTPE rev0 .z64 dump "
                         "(convert .v64/.n64 or zipped ROMs first)")
    return data


def record(rom: bytes, index: int) -> dict:
    offset = struct.unpack(">H", rom[TABLE + 4 * index:TABLE + 4 * index + 2])[0]
    address = RECORD_BASE + offset
    raw = rom[address:address + 20]
    text = ""
    if raw[2] & 0x30:
        end = rom.index(b"\0", address + 16)
        text = rom[address + 16:end].decode("latin-1").replace("-0", " / ")
    mask = raw[14]
    return {
        "entry": index,
        "flags": raw[2],
        "layers": raw[3],
        "colours": raw[5],
        "colour_types": [FIRST_COLOUR_TYPE + k for k in range(8) if mask >> k & 1],
        "byte4": raw[4],
        "byte9": raw[9],
        "byte10": raw[10],
        "byte12": raw[12],
        "group": raw[13],
        "text": text,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("rom", type=Path)
    parser.add_argument("--csv", action="store_true", help="CSV instead of Markdown")
    parser.add_argument("--all", action="store_true",
                        help="every table entry, not just Rescue chapters 1-15")
    args = parser.parse_args(argv)
    rom = load_rom(args.rom)
    rows = []
    indices = range(ENTRIES - 1) if args.all else range(
        RESCUE_FIRST, RESCUE_FIRST + RESCUE_CHAPTERS * LEVELS_PER_CHAPTER)
    for index in indices:
        r = record(rom, index)
        rescue = RESCUE_FIRST <= index < RESCUE_FIRST + RESCUE_CHAPTERS * LEVELS_PER_CHAPTER
        r["stage"] = (f"{(index - RESCUE_FIRST) // LEVELS_PER_CHAPTER + 1}:"
                      f"{(index - RESCUE_FIRST) % LEVELS_PER_CHAPTER + 1}") if rescue else ""
        rows.append(r)
    columns = ["entry", "stage", "layers", "colours", "colour_types", "byte12", "group",
               "flags", "byte4", "byte9", "byte10", "text"]
    if args.csv:
        print(",".join(columns))
        for r in rows:
            print(",".join(
                '"' + " ".join(map(str, r[c])) + '"' if isinstance(r[c], list)
                else f'"{r[c]}"' if c == "text" else str(r[c]) for c in columns))
    else:
        print("| " + " | ".join(columns) + " |")
        print("|" + " --- |" * len(columns))
        for r in rows:
            print("| " + " | ".join(
                " ".join(map(str, r[c])) if isinstance(r[c], list) else str(r[c])
                for c in columns) + " |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
