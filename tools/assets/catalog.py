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


def decode_mesh(data):
    """A 3D object (archive s2), as the game draws it (func_800331B0).
    Header: u32 offsets to sections A (0x18), B (materials: a u32 count
    first), C (parts), D (vertices: N64 vertices, 16 bytes: s16 x, y, z,
    flag, s, t, then 4 bytes, normals in lit models) and E, then the size.
    Part p's block is at C + (s16 at C + 2p): s16 batch count, s16 offset of
    its triangle bytes, then 6-byte batches: s16 first vertex, u8 first cache
    slot, u8 vertex count, u8 triangle count, u8 count of texture-coordinate
    tweaks (6 bytes each, after the batches; skipped here). Triangles are
    cache slots, 3 nibbles each, two triangles to 3 bytes.
    Returns (vertices [(x, y, z, r, g, b)], triangles [(i, j, k, part)]) or None."""
    if len(data) < 24:
        return None
    hdr = struct.unpack_from(">6I", data, 0)
    if hdr[0] != 0x18 or hdr[5] != len(data) or not (hdr[0] < hdr[1] < hdr[2] < hdr[3] <= hdr[4] <= hdr[5]):
        return None
    b_sec, c_sec, v_sec = hdr[1], hdr[2], hdr[3]
    parts = be32(data, b_sec)
    if not (0 < parts < 256):
        return None
    verts = []
    for o in range(v_sec, hdr[4] - 15, 16):
        x, y, z = struct.unpack_from(">hhh", data, o)
        verts.append((x, y, z, data[o + 12], data[o + 13], data[o + 14]))
    tris = []
    for part in range(parts):
        blk = c_sec + struct.unpack_from(">h", data, c_sec + 2 * part)[0]
        if blk < c_sec or blk + 4 > v_sec:
            return None
        batches, tri_off = struct.unpack_from(">hh", data, blk)
        if batches < 0 or batches > 512:
            return None
        t0 = blk + tri_off
        cache = [0] * 32
        nib = []  # the part's triangle nibbles, in order
        need = sum(data[blk + 4 + 6 * b + 4] for b in range(batches)) * 3
        for k in range((need + 1) // 2):
            byte = data[t0 + k] if t0 + k < len(data) else 0
            nib += [byte >> 4, byte & 15]
        n = 0
        for b in range(batches):
            e = blk + 4 + 6 * b
            first = struct.unpack_from(">h", data, e)[0]
            slot, count, ntri = data[e + 2], data[e + 3], data[e + 4]
            for k in range(count):
                if slot + k < len(cache):
                    cache[slot + k] = first + k
            for _ in range(ntri):
                a, b2, c = nib[n:n + 3]
                n += 3
                tris.append((cache[a], cache[b2], cache[c], part))
    if not tris or max(max(t[:3]) for t in tris) >= len(verts):
        return None
    # Material colours: section B is a u32 count, then u32 offsets (from B)
    # to each part's record, which starts with its RGBA colour.
    colours = []
    for part in range(parts):
        rec = b_sec + be32(data, b_sec + 4 + 4 * part)
        colours.append(tuple(data[rec:rec + 3]) if rec + 4 <= c_sec else (200, 200, 200))
    return verts, tris, colours


def render_mesh(mesh, size=200):
    """A shaded three-quarter view (painter's order), each part tinted."""
    import math
    from PIL import Image, ImageDraw
    verts, tris = mesh[0], mesh[1]
    colours = mesh[2] if len(mesh) > 2 else []
    # Leave out flat background cards (a part of a few triangles lying in one
    # plane) when the object has other parts, so the subject fills the view.
    by_part = {}
    for t in tris:
        by_part.setdefault(t[3], []).append(t)
    def flat(ts):
        idx = {i for t in ts for i in t[:3]}
        return len(ts) <= 8 and any(len({verts[i][axis] for i in idx}) == 1 for axis in range(3))
    keep = [p for p, ts in by_part.items() if not flat(ts)]
    if keep and len(keep) < len(by_part):
        tris = [t for t in tris if t[3] in keep]
    yaw, pitch = math.radians(35), math.radians(-20)
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    pts = []
    for x, y, z, *_ in verts:
        x1, z1 = x * cy + z * sy, -x * sy + z * cy
        y1, z2 = y * cp - z1 * sp, y * sp + z1 * cp
        pts.append((x1, y1, z2))
    used = {i for t in tris for i in t[:3]}
    xs = [pts[i][0] for i in used]
    ys = [pts[i][1] for i in used]
    span = max(max(xs) - min(xs), max(ys) - min(ys), 1)
    scale = (size - 16) / span
    ox, oy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    tints = [(200, 200, 210), (230, 150, 70), (90, 170, 230), (120, 210, 120), (220, 110, 170), (230, 220, 100),
             (150, 120, 230), (110, 210, 200)]
    im = Image.new("RGBA", (size, size), (24, 26, 40, 255))
    draw = ImageDraw.Draw(im)
    faces = []
    for a, b, c, part in tris:
        pa, pb, pc = pts[a], pts[b], pts[c]
        ux, uy, uz = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
        vx, vy, vz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1
        light = abs((nx * 0.3 + ny * 0.5 - nz * 0.8) / ln)
        shade = 0.35 + 0.65 * light
        base = colours[part] if part < len(colours) else tints[part % len(tints)]
        if max(base) < 24:
            base = (170, 170, 180)  # black: a textured material (textures not applied in previews)
        col = tuple(int(ch * shade) for ch in base) + (255,)
        depth = (pa[2] + pb[2] + pc[2]) / 3
        poly = [(size / 2 + (p[0] - ox) * scale, size / 2 - (p[1] - oy) * scale) for p in (pa, pb, pc)]
        faces.append((depth, poly, col))
    for _, poly, col in sorted(faces, key=lambda f: -f[0]):
        draw.polygon(poly, fill=col)
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
    meshes = []
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
            mesh = decode_mesh(unpacked) if name == "s2" else None
            if mesh:
                entry["kind"] = "mesh"
                entry["mesh"] = {"vertices": len(mesh[0]), "triangles": len(mesh[1]), "parts": 1 + max(t[3] for t in mesh[1])}
                im = render_mesh(mesh)
                im.save(folder / ("%03X.png" % i))
                meshes.append((i, im, len(mesh[1])))
            elif info:
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
    if meshes:
        from PIL import Image, ImageDraw
        cell, cols = 200, 6
        rows = (len(meshes) + cols - 1) // cols
        sheet = Image.new("RGBA", (cols * cell, rows * (cell + 16)), (24, 26, 40, 255))
        draw = ImageDraw.Draw(sheet)
        for k, (i, im, n) in enumerate(meshes):
            x, y = (k % cols) * cell, (k // cols) * (cell + 16)
            sheet.alpha_composite(im, (x, y))
            draw.text((x + 4, y + cell), "s2 %03X  %d tris" % (i, n), fill=(220, 220, 240, 255))
        sheet.save(out / "models-sheet.png")
    kinds = {}
    for e in catalog:
        kinds[e.get("kind")] = kinds.get(e.get("kind"), 0) + 1
    print(json.dumps(kinds))


if __name__ == "__main__":
    main()
