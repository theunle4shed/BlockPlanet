"""Catalogues the game's asset archives from the player's own ROM.

Private research tool: everything it writes (unpacked entries, PNG previews,
a contact sheet and a catalogue) goes under .local/ and must never be
committed (AGENTS.md, Game-derived content). Only BlockPlanet's own
description of what is where (entry numbers, kinds, sizes) belongs in docs.

Archives: a big-endian u16 count, then count + 1 big-endian u32 offsets
(from the archive's start). An entry starts with the number of passes and one
letter per pass, a u32 stream length, then blocks of "SQSH", u32 unpacked
size, u32 packed size and the data. Pass "J" (every entry of the main
archive) is a 16-bit LZ, ported from func_80081264: a byte with the top bit
set copies (byte & 0x7F) + 1 halfwords from an earlier position (the next
big-endian u16, a halfword index into the block's output); otherwise byte + 1
literal halfwords follow. Equal sizes mean the block is stored as is.

Usage: python3 tools/assets/catalog.py [ROM] [OUT]
  defaults: .local/m0/tetrisphere-us.z64 and .local/assets
"""
import json
import pathlib
import re
import struct
import sys

ARCHIVES = {0x736C50: "main", 0x7D6C00: "s1", 0x5921F0: "s2", 0x490B10: "s3"}
OTHER_ARCHIVES = {}


def be16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def be32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def unpack_j(src, packed):
    out = []
    i = 0
    while i < packed:
        c = src[i]
        i += 1
        if c & 0x80:
            n = (c & 0x7F) + 1
            off = src[i] << 8 | src[i + 1]
            i += 2
            for k in range(n):
                out.append(out[off + k])
        else:
            for _ in range(c + 1):
                out.append(src[i] << 8 | src[i + 1])
                i += 2
    return struct.pack(">%dH" % len(out), *out)


def unpack_s(src):
    """Pass "S" (func_800807E4): LZSS over a 4 KB window. A byte below 0x10
    is followed by byte + 1 literal bytes; otherwise (byte >> 4) + 2 bytes are
    copied from window position ((byte & 15) << 8) + the next byte. The window
    starts with a fixed pattern (func_80080688) and is written from 0."""
    window = bytearray(0x1000)
    for i in range(256):
        window[i] = i
        window[0x100 + i] = 0xFF - i
        for j in range(4):
            window[0x200 + i * 4 + j] = i
    for i in range(0x600, 0x1000):
        window[i] = i & 0xFF
    out = bytearray()
    w = 0
    i = 0
    while i < len(src):
        c = src[i]
        i += 1
        if c & 0xF0 == 0:
            for _ in range(c + 1):
                if i >= len(src):
                    break
                b = src[i]
                i += 1
                window[w] = b
                out.append(b)
                w = (w + 1) & 0xFFF
        else:
            n = (c >> 4) + 2
            off = ((c & 0xF) << 8) + src[i]
            i += 1
            for _ in range(n):
                b = window[off]
                window[w] = b
                out.append(b)
                w = (w + 1) & 0xFFF
                off = (off + 1) & 0xFFF
    return bytes(out)


def unpack_entry(blob):
    passes = blob[0]
    letters = blob[1:1 + passes].decode("latin-1")
    if letters == "S":
        length = be32(blob, 2)
        return unpack_s(blob[6:6 + length]), letters
    if letters != "J":
        return None, letters
    pos = 1 + passes
    length = be32(blob, pos)
    pos += 4
    end = min(len(blob), pos + length)
    out = bytearray()
    while pos + 12 <= end and blob[pos:pos + 4] == b"SQSH":
        unpacked, packed = be32(blob, pos + 4), be32(blob, pos + 8)
        data = blob[pos + 12:pos + 12 + packed]
        block = bytes(data[:unpacked]) if unpacked == packed else unpack_j(data, packed)[:unpacked]
        out += block
        if len(out) % 2:
            out += b"\0"
        pos += 12 + packed
    return bytes(out), letters


def image_info(data):
    if len(data) < 8:
        return None
    w, h, fmt = struct.unpack_from(">hhh", data, 0)
    if not (1 <= w <= 640 and 1 <= h <= 480):
        return None
    if fmt == 0 and len(data) >= 8 + w * h * 2 and len(data) <= 8 + w * h * 2 + 64:
        return w, h, fmt
    if fmt == 2 and len(data) >= 8 + w * h and len(data) <= 8 + w * h + 64:
        return w, h, fmt
    return None


def to_png(data, info, path):
    from PIL import Image
    w, h, fmt = info
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            if fmt == 0:
                v = be16(data, 8 + 2 * (y * w + x))
                px[x, y] = ((v >> 11 & 31) * 255 // 31, (v >> 6 & 31) * 255 // 31, (v >> 1 & 31) * 255 // 31, 255 if v & 1 else 0)
            else:
                g = data[8 + y * w + x]
                px[x, y] = (g, g, g, 255)
    im.save(path)
    return im


def load_sites(root):
    """Where the game loads each main-archive entry: func_8002A8B4 calls
    with a constant entry number (from the private generated code)."""
    sites = {}
    gen = root / ".local/m1/linux-generated/functions"
    if not gen.exists():
        return sites
    for f in sorted(gen.glob("*.c")):
        last_entry = None
        prev = None
        for line in f.read_text(errors="ignore").splitlines():
            m = re.match(r"\s*// (0x[0-9A-F]{8}): (.*)", line)
            if not m or m.group(0) == prev:
                continue
            prev = m.group(0)
            addr, ins = m.group(1), m.group(2)
            e = re.search(r"addiu\s+\$a1, \$zero, (0x[0-9A-F]+|\d+)", ins)
            if e:
                last_entry = int(e.group(1), 0)
            if "jal" in ins and "0x8002A8B4" in ins and last_entry is not None:
                sites.setdefault(last_entry, []).append(addr)
    return sites


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    rom = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / ".local/m0/tetrisphere-us.z64"
    out = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else root / ".local/assets"
    data = rom.read_bytes()
    sites = load_sites(root)
    catalog = []
    thumbs = []
    for base, name in ARCHIVES.items():
        count = be16(data, base)
        folder = out / name
        folder.mkdir(parents=True, exist_ok=True)
        for i in range(count):
            start, end = be32(data, base + 2 + 4 * i), be32(data, base + 6 + 4 * i)
            entry = {"archive": name, "entry": i, "packed": max(0, end - start), "loaded_at": sites.get(i, []) if name == "main" else []}
            if end <= start or end - start > 0x200000:
                entry["kind"] = "empty"
                catalog.append(entry)
                continue
            blob = data[base + start:base + end]
            unpacked, letters = unpack_entry(blob)
            entry["passes"] = letters
            if unpacked is None:
                entry["kind"] = "not unpacked"
                catalog.append(entry)
                continue
            entry["size"] = len(unpacked)
            (folder / ("%03X.bin" % i)).write_bytes(unpacked)
            info = image_info(unpacked)
            if info:
                entry["kind"] = "image"
                entry["image"] = {"width": info[0], "height": info[1], "format": info[2]}
                thumbs.append((i, to_png(unpacked, info, folder / ("%03X.png" % i))))
            else:
                entry["kind"] = "data"
                entry["head"] = unpacked[:16].hex()
            catalog.append(entry)
    for base, name in OTHER_ARCHIVES.items():
        catalog.append({"archive": name, "rom": hex(base), "entries": be16(data, base), "kind": "archive (pass S, not unpacked yet)"})
    (out / "catalog.json").write_text(json.dumps(catalog, indent=1))
    # A contact sheet of every picture, each labelled with its entry number.
    if thumbs:
        from PIL import Image, ImageDraw
        cell = 160
        cols = 8
        rows = (len(thumbs) + cols - 1) // cols
        sheet = Image.new("RGBA", (cols * cell, rows * (cell + 16)), (24, 26, 40, 255))
        draw = ImageDraw.Draw(sheet)
        for k, (i, im) in enumerate(thumbs):
            t = im.copy()
            t.thumbnail((cell - 8, cell - 8))
            x, y = (k % cols) * cell, (k // cols) * (cell + 16)
            sheet.alpha_composite(t, (x + (cell - t.width) // 2, y + (cell - t.height) // 2))
            draw.text((x + 4, y + cell), "%03X %dx%d" % (i, im.width, im.height), fill=(220, 220, 240, 255))
        sheet.save(out / "contact-sheet.png")
    kinds = {}
    for e in catalog:
        kinds[e.get("kind")] = kinds.get(e.get("kind"), 0) + 1
    print(json.dumps(kinds))


if __name__ == "__main__":
    main()
