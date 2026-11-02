# Game state for trophy events (BlockPlanet)

Guest addresses used by `GameEventWatcher` (`include/tetrisphere/game_event_watcher.h`) to turn play into trophy events. NTPE rev0 only. Found on 2026-11-01 by reading the HUD code in the generated C and by recording memory every second graphics task during a scripted Rescue game (drops every 3 seconds, hearts lost, game over, retry). Blocksphere's [m3-state-map.md](m3-state-map.md) gave the player context and score.

RDRAM is stored as 32-bit host-order words, so a byte address is read at `address ^ 3` and a halfword at `address ^ 2`.

## Confirmed

| Value | Address | Type | Evidence |
| --- | --- | --- | --- |
| Scene | `0x800E121C` | s16 | -1 for the whole of gameplay, including retries. Other values in menus and transitions. |
| Game mode | `0x800E44A8` | s16 | During play: 1 Rescue, 2 Time Trial, 3 Training, 4 Puzzle, 5 Hide & Seek, 6 VS CPU (one recording each, read with the screenshots). Menus also show 2, so read it only during play. |
| P1 context | `0x80110220` | base | Blocksphere's map. P2 is `0x80113488`. |
| Score | context `+0x2AF8` (`0x80112D18`) | s32 | 0 at game start, 1040 after a scoring drop. The HUD shows ten times this value ("10 400"). The HUD's animated copy is `0x800E241C`. |
| Hearts left | context `+0x2D7E` (`0x80112F9E`) | s16 | 3 at game start, then 2, 1, and -1 when the last heart is lost (game over). Refills to 3 at each retry, in the same frame play begins. |
| Hearts shown | context `+0x30FC` | s16 | Follows hearts left. The HUD animates a heart when the two differ (`func_80078BA8`). |
| Heart states | context `+0x2FFC + 0x20 × i`, i = 0..2 | u8 | 0 heart, 1 turning, 2 skull. |
| Current piece | `0x801127FB` | u8 | Piece type; 8 for about 0.1 s on every drop. |
| Retries | `0x800E088F` | u8 | Increments on each TRY AGAIN, not on the first game. |
| Chapter | `0x800E44A0` | s16 | Rescue and Hide & Seek chapter, 0-based. Writing 4 during play showed "5:1" in the HUD. Writing it before the level loads builds that chapter's sphere (chapter 10, 15 and 20 tested). |
| Level in chapter | context `+0x23BA` (`0x801125DA`) | s16 | Level within the chapter, 0-based; the puzzle number in Puzzle. Advanced from 0 to 1 when level 1:1 was cleared. |
| Time Trial clock | context `+0x2B0C` | s32 | Blocksphere's candidate. Counts down from 18 059 (1/60 s, about five minutes) in mode 2. |

## How levels are built

The P1 block grid at `0x80116BF0` (Blocksphere's map) holds 32×32 cells of s16 per layer, 2048 bytes per layer.

- **Layer 0 is the core:** a fixed repeating tile pattern (cell values 0x5A to 0x5D).
- **The playfield layers are generated randomly on each start.** Two starts of chapter 1 left layer 1 different in 887 of 1024 cells, with similar mixes of values.
- **The chapter sets the depth:** chapter 1 filled 2 layers (core and one playfield layer) and chapter 10 filled 6.
- Layer 9 held identical values in both runs (384 cells of 0x1010).

So levels are generated, not stored designs. Custom levels are possible in principle: BlockPlanet could write a designed grid before play starts, as `--start-chapter` does for the chapter. The meaning of each cell value (piece id, colour, crystal) still needs mapping.

## Inferred, not yet confirmed

| Value | Evidence |
| --- | --- |
| `0x801125DA` = puzzle number | The HUD draws this number only in mode 4 (Puzzle), in the level badge. |
| Catch-up cap applies to Puzzle | The cap is for mode 4. See [frame pacing findings](blockplanet-frame-pacing.md). |
| Puzzle solved = puzzle number advancing | Same field as the Rescue level. Not yet seen in play. |
| Highest chapter | Chapter 20 loads a sphere; the real number of chapters is not known yet. |

## Events the watcher reports

| Event | Detection |
| --- | --- |
| `mode_started` | Entering play with 3 hearts and score 0, or hearts refilling to 3 during play. |
| `piece_placed` | Current piece changes to 8. |
| `life_lost` | Hearts left decreases. |
| `game_over` | Hearts left becomes -1, outside VS. Reports `score` as the HUD shows it. |
| `level_completed` | In Rescue or Hide & Seek, chapter or level higher than at the last moment of play. Reports `chapter` and `level` (1-based), `score` and `hearts_lost` during that level. Seen in play: clearing 1:1. |
| `chapter_completed` | The chapter number went up. `final` is always 0 until the last chapter is known. |
| `puzzle_solved` | In Puzzle, the puzzle number went up. Not yet seen in play. |
| `vs_round` | In VS CPU, P1's hearts reaching -1 is a `loss`; P2's (context `0x80113488 + 0x2D7E`) is a `win`. The P1 side was recorded: hearts run out and refill each round. The P2 side is inferred from the same layout. |

Mode names are reported for modes 1 to 6. Practice, two-player VS and Lines are not identified yet and report events without a mode. Puzzle keeps hearts at 3, so it never reports `life_lost` or `game_over`.

## Still to find

- Mode numbers for Practice, two-player VS and Lines.
- The number of chapters, to report `final` on the last one.
- Combo size and multiplier.
- Magic use and magic type.
- Puzzle drops and slides.
- Game over for modes without hearts: Puzzle, and Time Trial when the clock runs out.

## How the sphere is drawn (culling research, in progress)

Started 2026-11-02 to understand why blocks near the edge of the view go missing or show inner layers, which the owner confirmed also happens on real hardware.

- **Cursor:** context `+0x2B1A` (x) and `+0x2B1E` (y), 0..31 on the grid, with copies 8 bytes apart. The sphere turns to keep the cursor centred. `0x8015F88A`/`0x8015F88E` hold cursor + 7, the origin of a 13×13 neighbourhood used by the drop and shadow logic (`func_8009AB60`, `func_8009AAA8`), not by rendering.
- **The sphere's block geometry is a fixed amount every frame.** Logging RT64 vertex loads in gameplay showed 67 loads of 16 vertices from a per-frame buffer (`0x80254000` to `0x80258180` in the sampled frames) in every frame, while rotating as well. The number does not depend on the view.
- **Writer:** a hardware watchpoint on that buffer stopped in `func_8009C330`, called by `func_8009E630` ← `func_800A348C` ← `func_800CD3B8` ← `func_80070118` ← `func_800CCE7C`.
- `func_800A348C` calls about ten sphere drawing passes once each (`func_8009E630`, `func_8009EB28`, `func_8009F024`, `func_8009F520`, `func_8009FA20`, `func_800A0ED8`, `func_800A18F4`, `func_800A1E58`, `func_800A2374`, `func_800A2E88`).
- `func_8009E630` loops 8 rows (counter `0x8015F836`, column in `0x8015F83A`). For each cell it calls `func_8009D90C`, the cell setup pass, which reads the **precomputed mesh table** (pointer at `0x800E44E4`; rows of 0xC0 bytes, 12-byte entries) and the column map, and writes the top block's corners, height (`0x8015EDB2`) and kind (`0x8015EDAE`) for the face functions. (An earlier note here read `0x8015EDB2`/`0x8015EDAE` as a column span; they are the cell's height and kind.)

**How the patch is built:**

- The mesh table (pointer at `0x800E44E4`, P2's at `0x800E44E8`; `0x801270F0` in the sampled run) is **16 × 16 grid points**. Each 12-byte entry holds two vectors along the same direction: a base point (direction ÷ 3) and a **layer step** (direction ÷ 10). Layer *n* of a column lies between base + (*n* − 2) × step and base + (*n* − 1) × step, so every layer has the same thickness along its ray, and neighbouring columns meet exactly because they share the corner rays. (An earlier note here called the second vector the core point; the cell setup pass shows it is the step.)
- It is **recomputed while the sphere rotates** by `func_800ADD6C` (a hardware watchpoint on the table stopped there; it did not fire during a minute without rotation). It interpolates a fixed base grid of unit direction vectors at `0x800E3CD8` (rows of 18 points, 6 bytes each) by the sub-cell rotation, with 16 × 16 output loops.
- The table spans about **158° in both directions**, about 10.5° per grid step (360° / 32 cells is 11.25°). So the game draws the cells within about 79° of the view centre.

**Revised conclusion:** the patch is almost a hemisphere, not a small window. The visible edge of the sphere, seen from a camera at a finite distance, falls a little inside 90°, so the **patch boundary sits just beyond the silhouette**. Cells at the boundary have neighbours the renderer never considers, which shows as missing walls, or walls drawn as exposed, at the edge of the view. A deep hole viewed at an angle exposes walls right at the boundary, which is the case the owner reported (also on real hardware).

**Fix options:**

| Option | Work | Result |
| --- | --- | --- |
| Extended, for example 20 × 20 points (about 200°) | Reimplement the table builder and the drawing passes with larger loops; move their buffers into memory above 8 MB | Boundary moves behind the silhouette, fixing the edge artifacts. About 1.6 × the vertices. |
| Full, 32 × 32 | Replace the game's sphere drawing with BlockPlanet's own, built from the block grid | Edge fix, rear and peek cameras, the far side visible through an opened core (darkened), and Collaborate outlines and halos. About 4 × the vertices. |

**Stage 1 progress:** the table builder is reimplemented in `src/graphics/sphere_mesh.cpp` and verified identical to the original (`BLOCKPLANET_SPHERE=verify`). The table is rebuilt every frame, not only while rotating. Two paths are not yet exercised in a verify run: the per-axis scale (`+0x2478` set) and the two-player offset.

The cell setup pass (`func_8009D90C`) is reimplemented in `src/graphics/sphere_cell.cpp` and verified identical: zero differing values over more than 131,000 calls each in chapter 1 and chapter 15, and replace mode draws the sphere correctly. What it does for the cell at (`0x8015F836`, `0x8015F83A`):

- Copies the layer step of the cell's four corners to the face functions' work area, and writes the top block's outer and inner face corners (above).
- Copies shading vectors: four corner entries from a 16-per-row table at `0x8015EED0`, and a row and a column entry from tables at `0x8015EDC0` and `0x8015EE20` (the second half of each negated).
- Reads the **column map** (pointer at `0x800E44DC`): one byte per board cell, height in the high nibble (0 counts as 1) and kind in the low nibble. The board cell is the grid cell plus a scroll offset (context `+0x2B20`, `+0x2B24`), wrapped to 32 × 32. Kind is stored plus one, with 8 capped to 7 and a flag set at `0x8015D770`.
- Points the drawing code at the top block's entries in three per-block planes (pointers at `0x800E44D0`, `0x800E44D4`, `0x800E44D8`, entries of 2, 1 and 4 bytes), which hold one 32 × 32 layer after another. The block index is stored at `0x8015F842`.

The verify harness in `src/graphics/sphere_hooks.cpp` works for any pass: BlockPlanet's version writes to an overlay of guest memory at the start of the original, and every value it would write is compared with guest memory before the original returns. The first mismatches are logged with address, expected and actual values.

Both need the game's drawing functions replaced with BlockPlanet code (N64Recomp can call host functions in place of recompiled ones). The fixed sizes live in the table builder (16 × 16 loops), the table storage (16 rows of `0xC0` bytes), the base grid (18 points per row), the drawing passes (8-row loops with column spans at `0x8015EDB2`/`0x8015EDAE`) and the per-frame vertex buffer.
