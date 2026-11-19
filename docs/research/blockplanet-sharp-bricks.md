# Sharp bricks

The owner's request (2026-11-19): the bricks blur in close views (a stage's clear, the drop timer's zoom); make them sharper with a solid outline.

## Why they blur

Every brick face is cut from a **brick sheet**: 256 × 16 texels in 4-bit colour indices, 16 tiles of 16 × 16 (the piece parts: ends, middles and fillers with their outlines, then patterned tiles, the rubble orb and others), drawn through a 16-colour palette bank, one bank per brick colour. In Rescue 1:1 the sheets are at `0x800E2AD0` and `0x800E32D0`, loaded with one LOADBLOCK (`F3000000 073FF080`: 1,024 16-bit texels, 16 words a line) into TMEM 0, palettes from `0x800E3AD8` and `0x800E9400` by LOADTLUT. In a close view one 16-texel tile covers 60 or more pixels, so the filtering smears it, its 1-texel outline included.

## How BlockPlanet sharpens them

RT64 can draw a replacement image in place of any texture it recognises by a hash of what the texture occupies in TMEM (its replacement packs). BlockPlanet builds such a pack from the player's own game while it plays, in the data folder (`texture-packs/sharp-bricks`), so nothing from the game is in the repository:

1. **Find the brick draws.** Every 15th frame the frame's display list is walked (`tetrisphere::bricks::find_draws`), following calls and segments, with a copy of TMEM kept up to date from the LOADBLOCK and LOADTLUT commands: a block load writes its texels in a run, swapping the two words of every odd line (by its dxt); a palette load writes each 16-bit colour four times. Each triangle drawn with a brick sheet's tile (colour index, 4-bit, line 16, 256 × 16, palette mode RGBA16 `0x8000`) is a brick draw.
2. **Hash it as RT64 does.** RT64's own `TMEMHasher::hash` (version 5) on that TMEM copy, with the tile's format, size, line, TMEM address and palette bank, the width and height (256 × 16) and the palette mode. Checked against RT64's texture dump (`BLOCKPLANET_TEXTURE_DUMP=dir`): the four brick textures of Rescue 1:1 (`c05858a83c6bf869`, `a1914cc01b0d404e`, `79a3fd06b2b9c16b`, `de467fb735b6dbb6`; palette banks 1, 2, 11 and 1) hash the same. Guest memory is kept word-swapped, so a byte at address a is at a ^ 3 in host memory.
3. **Make the replacement.** Decode the sheet through its palette bank, scale it up 8 times with Scale2x three times (a pixel-art scaler: edges stay hard, diagonals and corners are smoothed), and write a DDS (RGBA8, DX10 header) with 4 mipmaps, from 8 times down to the original size, so the sphere at its normal size is never coarser than before and does not shimmer. A PNG would get no mipmaps from RT64.
4. **Hand it to RT64.** The pack folder (with an `rt64.json` declaring hash version 5) is loaded at start-up (`TextureCache::loadReplacementDirectory`), and each new replacement is added with `TextureCache::addReplacement`. Made once; later runs reuse the file.

Cost: walking the display list every frame cost 0.46 ms a frame on average on the test machine (Rescue 1:1, software Vulkan, 600 frames); every 15th frame, 0.035 ms. A new brick colour is sharpened within a quarter of a second of first appearing.

## Styles (Graphics > Bricks, 2026-11-19)

- **Sharp** (the default): the original tiles scaled up with Scale2x, as above.
- **Crafted** (version 2, 2026-11-19, the owner's choice of style): each piece tile (three colours or fewer) redrawn at 8 times the size, square: the darkest colour is the outline, deepened; the original's mid-tone ring inside it becomes brick; and an 11-pixel bevel inside the outline, lighter on the top (x1.24) and left (x1.12), darker on the bottom (x0.78) and right (x0.88), the nearest edge deciding, so the bands meet on the diagonal at the corners; with a gentle light from above. Patterned tiles (rubble, crystals) keep Sharp's look. The first version traced the colour regions as rounded shapes; the owner preferred square bevelled corners, as the rounded ones left a corner of the outline showing. Files carry the style's version in their name, so a changed style is made again.
- **Original**: no pack (RT64's replacements cleared).

Each style has its own folder in the data folder (`texture-packs/sharp-bricks`, `texture-packs/crafted-bricks`); the choice is saved in the profile and applied by the renderer on its next frame, so it can be changed in play.

## Switches

- `BLOCKPLANET_BRICKS=original|sharp|crafted`: overrides the setting.
- `BLOCKPLANET_TEXTURE_DUMP=dir`: RT64 dumps every texture's TMEM under its hash (large: about 900 MB for a short session).
- `BLOCKPLANET_DL_TEX_LOG=1`: logs each distinct texture set-up used for triangles in a frame.

## Next

- The outline: thicker or darker, drawn at full resolution, if the owner wants more than the original's own edge made crisp.
- Smooth gradients (the rubble orb's glow) come out in steps with Scale2x; a scaler that blends (xBR) or a separate treatment for those tiles would help.
- Other sheets: the other episodes' and modes' bricks are found the same way if they use the same sheet size; check Episodes 2 to 10, Hide & Seek, Lines and VS.
- **Procedural bricks (the owner liked the idea):** BlockPlanet could draw each brick type itself (its colour from the palette, then a clean outline, bevel and highlight at any resolution) instead of scaling the original's tiles, either as generated replacement images (the same pack, made from a drawing instead of the scaled tile) or as BlockPlanet's own geometry on the sphere it already draws.
