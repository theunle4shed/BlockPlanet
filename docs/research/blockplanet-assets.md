# The game's asset archives (2026-11-07)

How the game stores its pictures, textures and 3D data, for finding and reusing its own assets (the title rebuild, new block types, the Media Player). Only descriptions live here; the assets themselves are unpacked from the player's ROM into `.local/assets/` by `tools/assets/catalog.py` and are never committed (AGENTS.md, Game-derived content).

## Archives

Each archive is a big-endian u16 entry count, then count + 1 big-endian u32 offsets from the archive's start. An entry is: the number of passes and one letter per pass, a big-endian u32 length, then the data. `func_80081BE8(packed, destination)` unpacks an entry, running each pass through a jump table at `0x800EE6EC` (letters A, D, F, H, J, R, S, W); `func_8002A8B4(archive, entry, 8)` loads and unpacks one (about 180 call sites, so many entries can be labelled by where they are used).

| ROM offset | Name in the tool | Entries | Pass | Contents (first look) |
| --- | --- | --- | --- | --- |
| `0x736C50` | main | 304 | J | 2D: 278 pictures (menus, HUD pieces, title, logos, cards) and some raw 44 × 44 textures, palettes and 8-bit images |
| `0x7D6C00` | s1 | 48 | S | small textures (32 × 32, 16-bit and 8-bit) |
| `0x5921F0` | s2 | 125 | S | 25 to 70 KB each, offset tables at the start: probably the LightWave scenes (the intro's is one) |
| `0x490B10` | s3 | 308 | S | 2 to 22 KB each, floats (coordinates, scales): probably the 3D objects |

## Passes

- **J** (every entry of the main archive): blocks of `SQSH`, unpacked size, packed size and data; equal sizes mean stored. The data is a 16-bit LZ (`func_80081264`): a byte with the top bit set copies (byte & 0x7F) + 1 halfwords from an earlier halfword index (the next big-endian u16) in the block's output; otherwise byte + 1 literal halfwords follow.
- **S** (the three other archives): LZSS over a 4 KB window (`func_800807E4`). A byte below 0x10 is followed by byte + 1 literal bytes; otherwise (byte >> 4) + 2 bytes are copied from window position ((byte & 15) << 8) + the next byte. The window starts with a fixed pattern (`func_80080688`: 0 to 255, 255 down to 0, each value four times, then a ramp) and is written from 0.

## Pictures

A picture is s16 width, s16 height, s16 format, 2 bytes, then pixels: format 0 is RGBA 5551 (16-bit), 2 is one byte per pixel (intensity), 3 is one byte per pixel (likely colour-indexed). Entry `0x15` is the title picture (270 × 205), `0x14` its shadow.

## Next

- Decode the s3 object format (floats for points; polygons and surfaces to find) and the s2 scenes, then render previews of every model, or have the game draw each one in a test scene and capture it.
- Label entries by their call sites (the tool records them for the main archive).
- Look for the title's teal crab robot and the Rocket magic's model among them.
