"""Extract the audio RSP microcode from the player's own ROM (private input).

The audio microcode RSPRecomp turns into C (config/recomp/audio-rsp.toml) sits
in the NTPE rev0 ROM as it runs: 3,568 bytes at ROM offset 0xB9B80. This writes
it to .local/m1/microcode/final-private/audio-ucode.bin (never committed), after
checking its SHA-256, so a fresh checkout can build from the ROM alone.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
OFFSET = 0xB9B80
SIZE = 3568
SHA256 = "30f68cb1f957fa85c2c19706bb32d59cf3354548a2fbb66ac9acc7a66a639470"
OUTPUT = ROOT / ".local/m1/microcode/final-private/audio-ucode.bin"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, default=ROOT / ".local/m0/tetrisphere-us.z64",
                        help="the NTPE rev0 ROM in .z64 (big-endian) byte order")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    data = args.rom.read_bytes()
    ucode = data[OFFSET:OFFSET + SIZE]
    if hashlib.sha256(ucode).hexdigest() != SHA256:
        print("The audio microcode does not match: the ROM must be Tetrisphere (US, NTPE rev0) in .z64 byte order.",
              file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(ucode)
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
