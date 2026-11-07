# The game's asset archives (2026-11-07)

How the game stores its pictures, textures and 3D data, for finding and reusing its own assets (the title rebuild, new block types, the Media Player). Only descriptions live here; the assets themselves are unpacked from the player's ROM into `.local/assets/` by `tools/assets/catalog.py` and are never committed (AGENTS.md, Game-derived content).

## Archives

Each archive is a big-endian u16 entry count, then count + 1 big-endian u32 offsets from the archive's start. An entry is: the number of passes and one letter per pass, a big-endian u32 length, then the data. `func_80081BE8(packed, destination)` unpacks an entry, running each pass through a jump table at `0x800EE6EC` (letters A, D, F, H, J, R, S, W); `func_8002A8B4(archive, entry, 8)` loads and unpacks one (about 180 call sites, so many entries can be labelled by where they are used).

| ROM offset | Name in the tool | Entries | Pass | Contents (first look) |
| --- | --- | --- | --- | --- |
| `0x736C50` | main | 304 | J | 2D: 278 pictures (menus, HUD pieces, title, logos, cards) and some raw 44 × 44 textures, palettes and 8-bit images |
| `0x7D6C00` | s1 | 48 | S | small textures (32 × 32, 16-bit and 8-bit) |
| `0x5921F0` | s2 | 125 | S | **3D objects** (118 meshes), loaded with `func_8002A100(entry)` |
| `0x490B10` | s3 | 308 | S | **LightWave scenes** (object positions, rotations, scales and keyframes), loaded with `func_8002A0C0(entry)`; `func_8002CF70(memory, scene, objects, 1, …)` builds a drawable scene from one of these and an array of s2 objects (the intro: scene `0x3A` with object `0x15`) |

## Passes

- **J** (every entry of the main archive): blocks of `SQSH`, unpacked size, packed size and data; equal sizes mean stored. The data is a 16-bit LZ (`func_80081264`): a byte with the top bit set copies (byte & 0x7F) + 1 halfwords from an earlier halfword index (the next big-endian u16) in the block's output; otherwise byte + 1 literal halfwords follow.
- **S** (the three other archives): LZSS over a 4 KB window (`func_800807E4`). A byte below 0x10 is followed by byte + 1 literal bytes; otherwise (byte >> 4) + 2 bytes are copied from window position ((byte & 15) << 8) + the next byte. The window starts with a fixed pattern (`func_80080688`: 0 to 255, 255 down to 0, each value four times, then a ramp) and is written from 0.

## Pictures

A picture is s16 width, s16 height, s16 format, 2 bytes, then pixels: format 0 is RGBA 5551 (16-bit), 2 is one byte per pixel (intensity), 3 is one byte per pixel (likely colour-indexed). Entry `0x15` is the title picture (270 × 205), `0x14` its shadow.

## 3D objects (archive s2)

Decoded from the game's mesh routine `func_800331B0`. Header: u32 offsets to sections A (`0x18`), B (materials), C (parts), D (vertices) and E, then the size.

- **B, materials:** a u32 count (one per part), then u32 offsets from B to each record, which starts with the part's RGBA colour (black for textured parts).
- **C, parts:** part *p*'s block is at C + (s16 at C + 2*p*): s16 batch count, s16 offset of its triangle bytes, then 6-byte batches: s16 first vertex, u8 first vertex-cache slot, u8 vertex count (a `G_VTX` of up to 16), u8 triangle count, u8 count of texture-coordinate tweaks (6 bytes each after the batches, sent as `G_MOVEWORD` to the cached vertices' S and T).
- **Triangles:** vertex-cache slots, 3 nibbles per triangle (two triangles in 3 bytes), sent as `G_TRI1`.
- **D, vertices:** standard N64 vertices, 16 bytes (s16 x, y, z, flag, s, t, then 4 bytes: normals in lit objects).
- Section A probably groups parts into objects, and E holds per-object draw records; not needed for previews.

`tools/assets/catalog.py` renders every object (three-quarter view, material colours, flat background cards left out) into `.local/assets/models-sheet.png`.

**First look (descriptions only):** tetromino pieces in 3D (the bar, T, L, square and S shapes); 3D lettering for EPISODE, WIN, LOSE and HIDE & SEEK; the N64 logo, controller, Controller Pak and a monitor; rescued-robot spheres in several colours, with and without antennae; a white and red **rocket** (s2 `0x5E`, the Rocket magic, matching the title's rockets); a mortarboard (the menu's character); orange rings and a gyroscope ring; a crown with a jewel; a green tentacled creature with eyes on stalks (s2 `0x4C`, a candidate for the title's crab robot); a rocket-shaped firework, a flying saucer, a spiky starburst and many simple shapes (spheres, cubes, cones, tori). Textured objects show grey until textures are applied.

## Next

- Apply textures (s1 and the main archive) and split objects with section A, for truer previews.
- Read the s3 scenes to see which objects each uses and where (the title's characters, the endings).
- Label entries by their call sites (the tool records them for the main archive).
- Confirm the crab robot (s2 `0x4C`?) and the gold robot; the rocket is s2 `0x5E`.
