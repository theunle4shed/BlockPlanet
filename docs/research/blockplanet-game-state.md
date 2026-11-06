# Game state for trophy events (BlockPlanet)

Guest addresses used by `GameEventWatcher` (`include/tetrisphere/game_event_watcher.h`) to turn play into trophy events. NTPE rev0 only. Found on 2026-11-01 by reading the HUD code in the generated C and by recording memory every second graphics task during a scripted Rescue game (drops every 3 seconds, hearts lost, game over, retry). Blocksphere's [m3-state-map.md](m3-state-map.md) gave the player context and score.

RDRAM is stored as 32-bit host-order words, so a byte address is read at `address ^ 3` and a halfword at `address ^ 2`.

## Confirmed

| Value | Address | Type | Evidence |
| --- | --- | --- | --- |
| Scene | `0x800E121C` | s16 | -1 for the whole of gameplay, including retries. Other values in menus and transitions. |
| Game mode | `0x800E44A8` | s16 | During play: 1 Rescue, 2 Time Trial, 3 Training, 4 Puzzle, 5 Hide & Seek, 6 VS CPU, 9 Lines (one recording each, read with the screenshots). Menus also show 2, so read it only during play. |
| Original menu cursor | `0x800E121C`, `0x800E1220`, `0x800E1222`, `0x800E1224` | s16 | Main menu row (SINGLE 0 … CLOSE GAME 5; −1 when no menu is shown, as in play); submenu row (Options and similar pages); player page row (START 0); player page mode list row (0 Rescue, 1 Hide & Seek, 2 Puzzle, 3 Time Trial, 4 VS CPU, 5 Lines). See [docs/UI.md](../UI.md#starting-modes). |
| P1 context | `0x80110220` | base | Blocksphere's map. P2 is `0x80113488`. |
| Score | context `+0x2AF8` (`0x80112D18`) | s32 | 0 at game start, 1040 after a scoring drop. The HUD shows ten times this value ("10 400"). The HUD's animated copy is `0x800E241C`. |
| Hearts left | context `+0x2D7E` (`0x80112F9E`) | s16 | 3 at game start, then 2, 1, and -1 when the last heart is lost (game over). Refills to 3 at each retry, in the same frame play begins. |
| Hearts shown | context `+0x30FC` | s16 | Follows hearts left. The HUD animates a heart when the two differ (`func_80078BA8`). |
| Heart states | context `+0x2FFC + 0x20 × i`, i = 0..2 | u8 | 0 heart, 1 turning, 2 skull. |
| Current piece | `0x801127FB` | u8 | Piece type; 8 for about 0.1 s on every drop. |
| Retries | `0x800E088F` | u8 | Increments on each TRY AGAIN, not on the first game. |
| Training type | `0x800E44AC` | s16 | 1 Basics, 2 Advanced, 3 Puzzle (the Training page's rows 0, 1, 2 on submenu row `0x800E1220`; Training is main menu row 4). Lessons, from the text picker `func_80088E50(lesson)`: Basics = intro (text `0x800EE7A4`), lives (`0x800EEC5C`), combos (`0x800EEED0`), magic (`0x800EF198`); Advanced = crystal pieces (`0x800EF318`), X count (`0x800EF44C`), fuse combos (`0x800EF774`); Puzzle = `0x800EF910`. |
| Training page | `0x80103958`, `0x8010395C` | u32 | Start and end of the instructor's current page of text. Pressing A pages through Basics (demos play by themselves); after the last page (`0x800EF2B4`) the game returns to the main menu, about 3.5 minutes of paging. Quitting from the pause menu (CONTINUE, SKIP, EXIT) earlier never shows that page, so it marks Basics as finished (`training_completed`). Training Puzzle has six pages (`0x800EF910` to the last, `0x800EFB00`) with one demo, and also returns to the main menu after its last page. |
| Pause row | `0x800E1228` | s16 | During play: −1 when not paused; 0 CONTINUE, 1 SKIP (Training only), 2 AUDIO, 3 EXIT. Setting it and pressing A chooses that item. While paused the game draws, through its shared helpers: the "paused" background (`func_800353F4`, image `0x80197C00` in the sample), the white line and button hints (`func_8003396C`), the menu text (`func_80038478`) and the instructor (three parts through `func_8002D93C`). Recompiled calls keep no return address, so draws are told apart by helper and arguments (`BLOCKPLANET_PAUSE_LOG=1`). |
| Game over (Rescue) | `0x800E1228`, P1 hearts | | The game-over menu (TRY AGAIN, EXIT, and TRAINING with a hint in some cases) uses the pause row while player 1's hearts (`0x80112F9E`) read −1. **TRY AGAIN restarts the chapter**: a game over at 2:3 went back to 2:1 with full hearts (2026-11-05). There is no restart from the pause menu in the original. |
| Chapter | `0x800E44A0` | s16 | Rescue chapter, 0-based. **Hide & Seek has its own chapter** (not yet found): starting it with this set to 2 still showed chapter 1 in its HUD, while the level (`+0x23BA`) was honoured. Its game over also goes back to the chapter's first level (tested 2026-11-05). Writing 4 during play showed "5:1" in the HUD. Writing it before the level loads builds that chapter's sphere (chapter 10, 15 and 20 tested). |
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

Mode names are reported for modes 1 to 6. Practice, two-player VS and Lines (mode 9) are not named yet and report events without a mode. Puzzle keeps hearts at 3, so it never reports `life_lost` or `game_over`.

## Still to find

- Mode numbers for Practice and two-player VS. (Lines is mode 9.)
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
| Full, 32 × 32 | Replace the game's sphere drawing with BlockPlanet's own, built from the block grid | Edge fix, rear and peek cameras, the far side visible through an opened core (darkened), and Cooperative outlines and halos. About 4 × the vertices. |

**Stage 1 progress:** the table builder is reimplemented in `src/graphics/sphere_mesh.cpp` and verified identical to the original (`BLOCKPLANET_SPHERE=verify`). The table is rebuilt every frame, not only while rotating. Two paths are not yet exercised in a verify run: the per-axis scale (`+0x2478` set) and the two-player offset.

The cell setup pass (`func_8009D90C`) is reimplemented in `src/graphics/sphere_cell.cpp` and verified identical: zero differing values over more than 131,000 calls each in chapter 1 and chapter 15, and replace mode draws the sphere correctly. What it does for the cell at (`0x8015F836`, `0x8015F83A`):

- Copies the layer step of the cell's four corners to the face functions' work area, and writes the top block's outer and inner face corners (above).
- Copies shading vectors: four corner entries from a 16-per-row table at `0x8015EED0`, and a row and a column entry from tables at `0x8015EDC0` and `0x8015EE20` (the second half of each negated).
- Reads the **column map** (pointer at `0x800E44DC`): one byte per board cell, height in the high nibble (0 counts as 1) and kind in the low nibble. The board cell is the grid cell plus a scroll offset (context `+0x2B20`, `+0x2B24`), wrapped to 32 × 32. Kind is stored plus one, with 8 capped to 7 and a flag set at `0x8015D770`.
- Points the drawing code at the top block's entries in three per-block planes (pointers at `0x800E44D0`, `0x800E44D4`, `0x800E44D8`, entries of 2, 1 and 4 bytes), which hold one 32 × 32 layer after another. The block index is stored at `0x8015F842`.

The four **quadrant passes** (`func_8009E630`, `func_8009EB28`, `func_8009F024`, `func_8009F520`) are reimplemented as one routine in `src/graphics/sphere_quadrants.cpp`, verified identical and working in replace mode. What they show:

- The game draws a **15 × 15 cell patch** (between the 16 × 16 mesh points) as four quarters: rows 0→7 or 14→8, columns 0→7 or 14→8. Each quarter runs from the patch edge towards the centre, so cells are drawn **far to near**.
- For each cell, the cell setup gives the column's first layer (`0x8015EDB2`) and end (`0x8015EDAE`); the loop lifts the face corners by one layer step per block and moves three per-block pointers on by one 32 × 32 layer (block value, 2 bytes; face mask, 1 byte; a 4-byte entry).
- **Exposed faces are precomputed** in the face-mask plane, one byte per block: `0x10` the outer face, `0x08`, `0x04`, `0x02`, `0x01` the four sides. Each quarter only draws the outer face and the two sides that look towards the centre of the view (masks `0x1A`, `0x16`, `0x19`, `0x15`). Sides facing away from the centre are never drawn, and neither is the inner (core-facing) face. This is the game's back-face culling. It is right for the normal camera, but a rear or peek camera, or a see-through core, needs all faces and a different drawing order.
- A block value of 0 is empty. Values `0x2D`, `0x12` and `0x3F` are drawn by their own functions (`func_8009E3E4`, `func_80097B74`, `func_80097848`); others go through `func_8009E314` (given the face mask), `func_8009E214`, then the face functions: `func_8009C330` (outer), `func_8009CE38` (`0x08`), `func_8009CA98` (`0x04`), `func_8009D570` (`0x02`), `func_8009D1D4` (`0x01`). The value ÷ 9 is stored at `0x8015F59C`.
- The passes write vertices from `0x80246000` and display list commands from about `0x80264000` (sampled run), besides their work area at `0x8015E5xx`–`0x8015F8xx`.

The verify harness in `src/graphics/sphere_hooks.cpp` works for any pass: BlockPlanet's version writes to an overlay of guest memory at the start of the original, and every value it would write is compared with guest memory before the original returns. The first mismatches are logged with address, expected and actual values. Passes that call other game functions (the quadrants) use a snapshot check instead: guest memory and registers are saved when the original starts; when it is about to return, BlockPlanet's version runs on a private copy of that snapshot and the copy is compared with the original's results. One quadrant call in nine is checked, so one check runs every few frames and the quarters take turns. The check must stay quick (a few milliseconds): it stalls the game thread, and a long stall (the first version took about 140 ms per frame) lets the runtime's message queues overflow, which loses messages and hangs the game. Live guest memory is never changed by the check. (An earlier version restored the snapshot over live memory, which could lose a message another host thread had posted into a queue in guest memory and hang the game.) The audio task and RT64's framebuffer copies can write guest memory during a check; differences in pages the pass never writes are reported separately as `outside_bytes_total`.

The six **face functions** are reimplemented in `src/graphics/sphere_faces.cpp`. Each draws one face of the current block as a quad: when the 16-vertex batch (`0x8015F84E`) is full it first appends a load of the next 16 vertices (`0x04F00100`), then two F3D triangle commands (`0xBF000000`, slots × 10), then four vertices at `0x8015F848` (position from the face corners, texture coordinates from a per-block 0x50-byte table × 64, colour from corner or side shading, alpha from `0x8015EDB6`), and adds 4 to the vertex count at `0x800E44F0` in its return's delay slot. The inner (core-facing) face uses the bottom corners with negated shading; the quadrant passes never draw it.

**Status:** verified identical (every face check in chapters 1 and 15 showed zero differences, over 131,000 outer faces and thousands of each side face), and replace mode plays normally with the whole sphere drawn by BlockPlanet code (mesh, cell setup, quadrant loops and faces). Only the special-block functions (`func_8009E3E4`, `func_80097B74`, `func_80097848`) and the per-block setup (`func_8009E314`, `func_8009E214`) are still the game's own.

`BLOCKPLANET_SPHERE_VERIFY` limits verify mode to some groups (comma-separated: `mesh`, `cell`, `faces`, `quadrants`). With only the quadrant check on, about 1 check in 100 differs by one display list word where a special block's triangle and a side face's triangle trade places. `BLOCKPLANET_SPHERE_SELFTEST=1`, which replays the original function itself instead of BlockPlanet's, shows the same differences at the same rate. So they come from the check (some input changing between the snapshot and the original's run), not from the port. Their cause is not known yet.

Both need the game's drawing functions replaced with BlockPlanet code (done in stage 1). The fixed sizes live in the table builder (16 × 16 loops), the table storage (16 rows of `0xC0` bytes), the base grid (18 points per row), the quadrant loops (15 × 15 cells) and the per-frame buffers.

### The base grid (stage 2, step 2)

The base grid at `0x800E3CD8` (17 rows of 18 points, 6 bytes each) is fixed data: only the mesh builder reads it, and nothing writes it. Turning the sphere does not move it; the board scrolls through it instead (context `+0x2B20`/`+0x2B24`), with the mesh builder interpolating between grid points for the sub-cell part.

- The points lie on a sphere with centre (0, 0, −10764) and radius 32856, fitted to within 49 units. Their lengths from the origin vary (about 22,000 at the middle to 40,000 at the corners) because the mesh's rays start at the origin, in front of the sphere's centre.
- Seen from the sphere's lowest point S = (0, 0, −43620), the grid is a uniform angular grid. Row *r* lies in the plane through S tilted by (8.5 − *r*) steps about the y direction (each row is flat to about one unit). Column *c* is (8.5 − *c*) steps around that row's circle as seen from S (twice that around the circle's own centre). The step is 5.4532°, about 10.9° as seen from the sphere's centre.
- The formula reproduces the table to within 49 units (0.15%), with the error varying smoothly by row; the table was probably made with a less precise sine table. Six points at the middle of the patch edges are clamped to the 16-bit limit.
- `include/tetrisphere/sphere_grid.h` implements the formula and an extended grid that uses the table's own values where it has unclamped ones and the formula elsewhere. In verify mode the game logs `sphere_grid_formula_check` once (300 points compared, largest difference 49).

The rule only covers the side of the sphere facing S, so it suits extended patches but not the full sphere. The board is 32 × 32 cells and wraps, so a patch can be at most 32 cells across before cells repeat.

### Normals (`func_800A2374`)

Runs just before the quadrant passes and fills the shading tables the cell setup copies from (the vertex colour bytes are lighting normals):

- `0x8015EED0`: for each of the 256 mesh points, −120 × the layer step divided by its length (rounded toward zero), in vertex component order.
- `0x8015EE20` (column sides, 15 entries): +120 × the normalised difference between neighbouring outer points along the middle mesh row (row 7).
- `0x8015EDC0` (row sides, 15 entries): the same along the middle mesh column (column 7).

It uses the work area (`0x8015F7CC` and nearby, `0x8015F842`) as scratch and `func_800D5100` for the square root.

### Extended patch (stage 2, step 3)

`BLOCKPLANET_SPHERE=replace BLOCKPLANET_SPHERE_RING=n` (n = 1 to 4) draws n extra cells on every side of the game's 15 × 15 patch (ring 2 gives 19 × 19). Single-player only for now; two-player games keep the original patch.

- Each frame, alongside the game's own mesh, `ExtendedSphere` (`include/tetrisphere/sphere_extended.h`) builds a (17 + 2n)² point mesh from the extended grid and the same normals as `func_800A2374`. It lives on the host; the game's 16 × 16 tables are left for its other passes.
- `prepare_extended_cell` writes the same work-area values as the game's cell setup, and `draw_extended_quadrant` runs the quadrant loops from −n to 14 + n, still far to near. The game's face functions draw the cells unchanged.
- Vertices go to BlockPlanet buffers above 4 MB (`0x80400000` and `0x80600000`, 2 MB each, one per game frame buffer). At the switch to and from them the vertex batch is forced full, so the next face loads a fresh batch and no triangle mixes buffers. Afterwards the game's vertex count is reset, so the game's pool is only moved on by the passes that follow. RT64 draws from these addresses.
- Display list commands stay in the game's display list (the sphere's share grew from about 7.7 KB to 12.6 KB in chapter 15 with ring 2); the loops stop adding cells if the frame's display list passes 96 KB of its 112 KB.
- Checked: with `BLOCKPLANET_SPHERE_EXTENDED_TEST=1` in verify mode, the extended path with no extra cells is compared with the game's quadrant passes. 128 checks in chapter 15 showed zero differing bytes, so the extended mesh, normals and cell setup match the game exactly. With ring 2 the sphere draws correctly in chapter 15. Whether it removes the edge artifacts the owner saw needs a playtest: dig deep near the edge of the view and turn the sphere.
- Known difference: the game's normal for the far row edge of row 14 reads an unrelated value (`0x8015EE1A`, the next table entry does not exist); the extended patch computes it. The quadrant passes never draw a face that uses it, so the original patch is unaffected.

### Layers, core size and stacking (2026-11-02)

**The board has 9 layers, 0 to 8**, each 32 × 32 cells (1,024), held in parallel tables: block values (2 bytes per cell, `0x800E44D0`, 0x4900 bytes), face masks (1 byte, `0x800E44D4`), a 4-byte entry (`0x800E44D8`, 0x9200 bytes), and the column map (`0x800E44DC`, one byte per column). Addresses in a gameplay dump: `0x80116BF0`, `0x8011B4F0`, `0x8011D9F0`, `0x80126BF0`.

- `func_8008B0E0` builds a column's map byte from the face masks: the high nibble is the lowest layer with an exposed face (searching layers 0 to 7), the low nibble the highest (searching 8 down to 0). The quadrant loops draw that range.
- Layer 8, above the normal surface, is drawn by its own pass, `func_8009FA20`, which runs only when the cell setup has seen a column reaching it (the "capped at 7" flag at `0x8015D770`).
- Rescue levels use at most 6 playfield layers plus the core, so layer 8 is the only headroom.
- The board helpers (`0x8008A7A0` to `0x8008CA34`, 42 functions) are called from about 70 functions across the game (block values via `func_8008A7A0` from 49, block types via `func_8008A7EC` from 28, face masks via `func_8008C9CC` from 17 and others). The 1,024-cell layer size, the 9-layer bounds and the 4-bit layer numbers in the column map are built into this code.

**Sphere proportions live only in the mesh builder.** Its divisions by 3 (base) and 10 (layer step) are the only ones in the game that shape the sphere (other divisions by 10 split numbers into digits). Everything placed on the sphere reads the mesh table (`func_80097E3C`, `func_80098D8C`, `func_8009D90C`, `func_8009FA20`, `func_800A0ED8`, `func_800A2374`) or the work area the cell setup fills (`func_800A18F4`, `func_800A1E58`, which call the face functions). So resizing the core or the layers in the mesh builder moves everything together. The top of layer *n* is at base + (*n* − 1) steps: the core's outer surface at 1/3 of the grid point's length, layer 8's top at 1.033, about 3.1 times the core's radius.

**Experiment:** `BLOCKPLANET_SPHERE_CORE_SCALE` (replace mode, 0.5 to 2.0) scales the core while keeping the top of layer 8 in place, so the layers get thinner. At 1.6 in chapter 1 the sphere draws and plays correctly with a visibly bigger core; because chapter 1 uses few layers, its visible surface also moves outwards. Not yet checked: the rescued character shown in the core, the camera's zoom when the core opens, and effects.

**What each change would take:**

| Change | Work | Main risks |
| --- | --- | --- |
| Bigger core, same board | A mesh builder setting (done as an experiment); decide which surface stays fixed | The rescued character's size and placement in the core, the core-opening camera, effects |
| More core cells, or a bigger board | The 32 × 32 board is built into the game logic (wrapping with `& 0x1F`, `<< 5` indexing) | Very large |
| Taller stacks (more than 9 layers) | Move the four tables to memory above 4 MB (about 7 KB per extra layer) and change every bound in about 70 functions: generation, gravity, matching, explosions and magic, landing, game over, scoring; widen the column map past 4 bits for more than 15 layers | Large; also the camera, since higher layers come closer to it |

### Buffer budget (stage 2, step 1)

Measured on 2026-11-02 with `BLOCKPLANET_SPHERE_STATS=1` (chapter 15, Linux test setup):

| Buffer | Copies (double-buffered) | Size each | Peak use, whole frame | Peak use, sphere |
| --- | --- | --- | --- | --- |
| Vertex pool (pointer `0x8013DCF8`, frame start `0x8013DCFC`) | `0x80246000`, `0x80254000` | 56 KB (3,584 vertices) | 22.9 KB (41%) | 18.0 KB (about 1,120 vertices) |
| Display list (pointer `0x800F22B4`, frame start `0x800F22B8`) | `0x80262000`, `0x8027E000` | 112 KB | 38.2 KB (34%) | 9.5 KB |

Menus use smaller buffers (vertices at `0x80266000`/`0x80268000`, display list from `0x8026A000`). The buffers are set per frame from the task structures at `0x800FACC0` (`+0x18E8` display list, `+0x18EC` vertex pool; the second at `+0x3200`/`+0x3204`). After the sphere, `func_800CD3B8` moves the vertex pool on by the vertex count at `0x800E44F0` × 16.

- **An extended patch fits the game's own buffers, just.** 19 × 19 cells is about 1.6 times the 15 × 15 patch's vertices: roughly 29 KB for the sphere and 34 KB for the frame, 61% of the pool. A heavily dug stage could push past it, so this is not a safe home.
- **The upper 4 MB is free.** The game is written for 4 MB: in a gameplay memory dump everything from `0x80400000` to `0x807FFFFF` is zero. The code's only references to that range are the constant `0x80400001` in comparisons, which look like pointer-range checks rather than memory use. The runtime reports 8 MB (`osMemSize`). BlockPlanet can keep its own double-buffered sphere vertex buffers there, sized for the full sphere, and leave the game's pool untouched.
- Display list commands for the sphere can stay in the game's display list (9.5 KB of 112 KB, with room for several times that), or move up as well for the full sphere.
- Still to confirm in practice: that RT64 draws vertices from above 4 MB (it supports 8 MB for Expansion Pak games), and that no game mode uses the upper 4 MB (only gameplay was dumped).

### Puzzle boards: the purple border (2026-11-05)

A memory dump of Puzzle 1 (F2 with `BLOCKPLANET_DUMP_DIR`, see below) shows how a puzzle limits the play area. Layer 0 is the core (tile values 81 to 89). Layer 1 holds **924 cells of block value 423 (type 47: the type is value ÷ 9, `func_8008A7EC`)** covering the whole 32 × 32 board except a 10 × 10 window (rows and columns 11 to 20). The window holds 84 empty cells, through which the core shows white, and the puzzle's piece (values 297 to 300, type 33). Layers 2 to 8 are empty. The filler cells have only their outer face exposed (face mask 16), except the ones edging the window.

So the purple "out of bounds" is ordinary board blocks of one type. The level builder `func_800857E8` creates the type 47 descriptor (value 423, type 47, third field 1, at `0x800885CC`). Gameplay code compares against 47 almost nowhere, so its behaviour presumably comes from per-type data. **Experiment (2026-11-05):** `BLOCKPLANET_TEST_TYPE47=1` turned the blocks in every other 4-column stripe of a Rescue 1:1 board into type 47 (512 cells) when play began; then five drops with the sphere turned, a drag attempt (B held with up) and a game over, with a memory dump after each step:

- **Drawn as Puzzle's purple border:** the stripes look the same as in Puzzle, so the look comes from the type.
- **Never removed:** all 512 cells stayed, in the same places, through every drop and combo. Meanwhile 129 normal blocks on that layer and 36 core cells were cleared.
- **Pieces land on it:** dropped pieces stacked on top (cells appeared on layers 2 and 3).
- **Not moved by the drag attempt**, though it is not certain the cursor was on a purple block at the time.
- **Side effect of the test:** converting only part of some pieces set off a large clear-up as play began (score 20,000 before the first drop). The game seems to clean up broken pieces (compare the "NOT IMPLODING A PIECE BECAUSE IT IS BROKEN" debug string), so a mode using type 47 should place it as whole cells of its own, as Puzzle does, not by cutting existing pieces.
- Lives were lost on most drops, but random drops also lose lives in normal play, so that is not attributed to type 47.

So type 47 behaves as an inert wall: a floor pieces can land on that nothing clears. Still open: whether a block can be dragged onto or across it, and whether magic affects it.

Research aids: `BLOCKPLANET_WATCH="0x800E44A8:2,…"` logs guest values as they change; with `BLOCKPLANET_DUMP_DIR` set, F2 saves the 8 MB of guest memory there (32-bit words in host order: bytes at address ^ 3, halfwords at ^ 2).

### The pause background (2026-11-05)

- **The squares** are the same in every mode (Story, Training, Puzzle) and do not show the board, so they are a fixed backdrop, not a copy of the game frame. They move, zoom and change colour while paused, keeping their spherical bulge, and the main menu uses the same kind of backdrop in blue. They are not drawn through any of the hooked 2D helpers (`0x8003396C`, `0x800353F4`, `0x80036FE4`, `0x80038478`, `0x80071DE8`, `0x8007240C`) and not by the board sphere's quadrant passes, so they are most likely a 3D model (a textured sphere) drawn by another routine, not yet found.
- **The "paused" lettering** is slanted in perspective and disappears together with the instructor when the model helper `func_8002D93C` (object `0x801AC880`, second argument 2 to 12, which changes with time like an animation frame) is skipped. Skipping any single value of that argument leaves both visible. Separating the lettering from the instructor needs more work.
- `BLOCKPLANET_PAUSE_KEEP_ALL=1` turns off the pause menu's hiding, for these experiments.

### Audio settings (2026-11-05)

The original Audio page (Options > Audio, or Audio in the pause menu) has three rows: a **music/fx** balance slider, **selection** (AUTO, RANDOM, CHOOSE) and **track**.

| Address | Size | Meaning |
| --- | --- | --- |
| `0x800E0B1C` | s32 | Balance, 0 to 12 (6 is the middle). |
| `0x800E0B20` | s32 | Selection: 0 Auto, 1 Random, 2 Choose. |
| `0x800E0B24` | s32 | Track, 0 to 22. Names through the pointer table at `0x800E0B28`: Prophetic - Title, Azule Lux, Martist, Compliant Confuse, Faze, Flim Flam, Manicdrummix, Hallucid, Perpetuate303, Phony, T. Grey, Learn, Zyp Zyp, Extol, then Magic Fluter, Spooky Baby, Zooming Car, Cheddar Funk, Snowy Mushrooms, Dog Robot, Sappy, Skank Bowl Fight and Birthday Bumps (these nine end in `$` in the data, perhaps marking hidden tracks). |
| `0x80102B08` | u8 | In the cartridge save data (base `0x80102918` + `0x1F0`): selection << 4 \| balance. |
| `0x800E28E8` | u16 | Music volume, min(0x4FFF, 0xA008 − 3414 × balance): full up to 6, silent at 12. |
| `0x800E28EC` | u16 | Effects volume, min(0x7FFF, 0x1556 × balance): silent at 0, full from 6. Written only. |

Writing the balance alone changes nothing: the page's handler (around `0x800552E4`) also calls `func_80079B80(music volume, music player 0x80107290)`, which sets the player's volume (`+0x1578`, with `+0x2000` for track ids 14 to 99) and a changed flag (`+0x1583`), and `func_80079BD4(effects volume)`, which updates the playing voices. BlockPlanet does the same from a hook at the top of the controller and save thread's loop (`func_80029938` at `0x800299A0`, about 60 times a second in every scene). `func_8003FE3C` (the menu driver) and `func_8002B348` do not run at the title screen, so they cannot carry a per-frame hook.

**Where the game changes the music volume** (callers of `func_80079B80`, 2026-11-05): the Audio page; the menu screens; song changes, which fade the new song in over 20 steps up to `0x800E28E8` (`func_8007FE4C`, called from gameplay by `func_800906E8` when the level's song changes); two routines that restore the levels from the balance (`func_80066F18`, `func_80067434`); audio setup (`func_8007F0D0`); and **jingles**, which play on separate music players (`0x80108828`, `0x80109DC0`) set to full volume (`0x7FFF`), for example from the HUD routine `func_80075CD8`. Nothing in gameplay turns the background music down for an effect such as magic. The original balance did not affect the jingles. BlockPlanet's Music setting scales every call, jingles included, where `func_80079B80` stores the final volume (`0x80079BB4`, after it adds `0x2000` for some songs), and its Effects setting scales `func_80079BD4`'s argument. The game's volume acts roughly on power (half the value gave a quarter of the measured loudness), so a setting of p% scales by √(p/100).

Playing a track, as the page's track row does: set `0x800DFE84` to 1, call `func_8007F720(track, music player)` then `func_8007ACFC(music player)`, clear `0x800DFE84`, store the track at `0x80102B09` (save data) and 15 at `0x800E14A8`. BlockPlanet keeps the balance at 6 (both full) and scales the volumes as described above.

Research aid: with `BLOCKPLANET_POKE_FILE` set, lines `0xADDRESS:SIZE=VALUE` written to that file are stored into guest memory.

### Camera and framing (2026-11-05)

**Correction (later the same day): the sphere is drawn through a perspective camera after all.** The drop-timer zoom revealed it: the model matrix in the per-frame graphics data at `+0x40` (pointer `0x801028D4`) places the sphere about 2,617 units from the camera, and the player context's **camera distance at `+0x2420`** (`0x80112640` for player 1, float; the camera's position at `0x801309E0` follows it) drives it. The earlier experiment widened the wrong perspective matrix (the 60° one built by `func_800C92C0`), and the orthographic projections are for other layers. The notes below about "no camera" are superseded; the mesh scale still works but the natural control for framing, the drop push and a peek camera is this distance.

**The drop timer zoom** (Rescue 1:1, no input): the drop timer (`+0x2B10`, high half; 0 to 600 in `func_800CD3B8`'s units, with warning sounds at 189 and 389 and faster drops at 200 and 400) counts up while the player holds a piece. Near the end the camera closes in: camera distance about 2,423 (sphere at 2,617) until the timer's last stretch, then 1,989, 1,545 and 1,119 (sphere at 2,173, 1,706, 1,252) as time runs out, about **twice the normal size** at the end; after the piece drops it springs back over about a second. The ramp is visible in the screenshots and memory dumps of that run.

**The game has no moving camera for the sphere.** Every frame `func_800699B4` builds orthographic projections (guOrtho, `func_800D5094`) into the per-frame graphics data (pointer at `0x801028D4`): all with extents −160..160 × −120..120 (a 320 × 240 view), at `+0x300` (near/far ±1024), `+0x1080` (±1000), `+0x1640` (−1000..0) and `+0x1680` (0..1000). The only perspective projections in play (guPerspective, `func_800D47E0`, 40° at 4:3) are for other models, not the sphere. The sphere's apparent 3D shape and size come entirely from its mesh (the base grid times the per-frame mesh table).

Experiments: scaling the `+0x1640` projection's extents (zoom out 1.5×, by hooking guOrtho) shrank only some elements; scaling the other projections shrank the HUD-side models and pieces too, never the sphere alone. Widening the 40° perspective changed nothing visible on the sphere.

**What does change the sphere's size: its mesh.** The mesh builder already supports a per-axis scale (player context `+0x2478` flag, `+0x247A/C/E`, `0x4000` = 1.0; the game only ever shrinks with it). Applying a uniform scale on top (BlockPlanet's renderer, `sphere::set_view_scale`, `BLOCKPLANET_SPHERE_SIZE` for testing) resizes the whole sphere around its centre, and everything placed on it (cursor, the falling piece's landing spot, effects) follows, because they all read the same mesh table. Checked at 0.8 and 1.15 in Rescue 1:1, including a drop: the piece lands correctly at both sizes. Limits: only with Sphere = Extended (BlockPlanet's renderer); the pieces in flight above the sphere and the background are not scaled.

So framing (sphere size for the screen, the drop push, a zoom option) is done through the mesh scale, and a peek camera would be a rotation of the mesh rather than a camera move.

### Between-chapter scenes, endings and credits (2026-11-05)

**Messages.** After a chapter (the game says "episode"), `func_80063F1C` (called from the menu driver `func_8005907C` when the menu row `0x800E1228` is 100) shows a scene with two pictures and a message. Message tables by mode, indexed by the chapter number `0x800E44A0` (VS CPU: chapter − 1); pictures from the image ids at `0x800E0AC0` and `0x800E0AE8`, song from `0x800E1550` (ids 314–317).

| Mode | Table | Messages |
| --- | --- | --- |
| Rescue | `0x800E150C`, 9 | 0 "Congratulations you passed the first episode. Ready to try the next one?" · 1 "Episode 2 down. It's not always going to be this easy." · 2 "Great! Have you tried gravity combos??? Keep playing for the victory." · 3 "Almost there! Beat this for a great ending!" · 4 "Still with me? There is an even greater reward ahead, but it gets real hard from here! Only fuse combos can help get your X count above 10" · 5 "If you do any gravity combo or fuse combo, it will triple your time return on power pieces! Finishing the bonus episodes will give you a code." · 6 "Final victory! Beat this and you will get the final victory, and access to the bonus episodes!" · 7 "Bonus episode. Only true Tetrisphere champions should try this episode. Did you know you can create power pieces with your shadow?…" · 8 "That was amazing! Enter your name as LINES for a hidden game! The ultimate challenge of episode 10 awaits." |
| Hide & Seek | `0x800E14FC`, 4 | "…you finished the first episode. The real challenge awaits…" · "Now you understand. Have you tried advanced training?…" · "…you've made it this far!… enter your name as CR… to see who we are" · "O.K. now you're getting good! But this final challenge awaits… try pressing L, C-right and C-down when entering your name." |
| Lines | `0x800E1530`, 4 | "…passed the first episode…" · "Episode 2 down…" · "Almost there, now it gets real tough." · "Way to go!! Try entering +;<>^ as your name for access to any level!" |
| VS CPU | `0x800E1540`, 4 | "Congratulations, but we're just getting warmed up." · "Great! Finish this for a victory!" · "Good job! You've beaten us. Now you can challenge the master." · "Beat this episode to be the ultimate Tetrisphere champion!" |

**Endings.** In `func_8005907C` (`0x800591D4`), when the last level of a chapter is finished, `func_80057128` (the ending, with the credits) runs if the chapter just finished is the mode's **first ending** chapter (`func_80089D04`: Rescue 5, Hide & Seek 3, VS CPU 3, Lines 3) or, except in Hide & Seek, its **final** chapter (`func_80089C40`: Rescue 8, VS CPU 5, Lines 5). Otherwise the message scene runs.

**Confirmed in play** (each level won by clearing the board through the poke file, in casual mode): Rescue chapter 1 has **10 levels**; after 1:10 the game shows its chapter-end menu (CONTINUE, EXIT, with the rescued robot; pause row `0x800E1228` = 0, **without a Start press**), then the message scene (row 100: an "EPISODE 2" card with message 0), then 2:1. **Both endings watched in play** (a run winning every level in turn, about 40 seconds a level, with A pressed now and then while waiting, which may have skipped the credits): after chapter 5, an ending cutscene (the rescued robots together on a planet), a short transition, then the "EPISODE 6" card with message 4 ("Still with me?… it gets real hard from here!"). After chapter 8, the final victory: a cutscene on a theatre stage with curtains, then the "EPISODE 9" card with message 7 ("Bonus episode. Only true Tetrisphere champions should try this episode…"). The credits themselves are still to be watched (a run without pressing A). Forcing a level's end by writing the level counters does not work: `0x800E4498` and the values that move with it (`0x800DFDA2`, `0x800E088E`, `0x800F2126`, `0x801029EE`, the context's `+0x23BA`, `+0x25C4`, `+0x2C82`) are rewritten from a count not yet found, and overriding `func_80089C9C` changed nothing.

So **Rescue has 10 episodes**: 1–5, then the first ending with credits ("great ending"), then "it gets real hard from here" for 6–8, the final victory (second ending) after 8, and **bonus episodes 9 and 10**. BlockPlanet's Story chapter list is capped at 10.

**Levels per chapter** (`func_80089C9C`, by mode): Rescue 10 (confirmed), Hide & Seek 30, VS CPU 7, Lines 10, Puzzle 100 (its puzzle count). **The level BlockPlanet writes (`p1 context +0x23BA`) only changes the display**: starting at "1:10" and winning went on to "1:2", because the game keeps its own count of levels won in the chapter (not yet found; `0x800E4498` is overwritten every frame). Level select and Restart level for a level other than the first need that count.

**Cheats in the messages:** name CR… (credits), LINES (the hidden Lines game), +;<>^ (any level), L + C-right + C-down at name entry; "finishing the bonus episodes will give you a code".

**How big the original draws the sphere** (measured at 16:9, where the picture's height matches the original 4:3 height): at stage 1:1 the sphere is about **55% of the screen height**, centred (about 22% above and below). At 10:1 it is **larger than the screen**, overflowing the top and bottom, because deeper stages have more layers on the same core. So the original's sphere size varies with the stage. In windows wider than 4:3 (Fill window), the game keeps the same size relative to the height, so the original framing already holds; in windows narrower than 4:3 (portrait) the picture is boxed and the sphere is smaller relative to the window.

### The piece queue (2026-11-05)

Found for the HUD's piece sprites (Rescue 1:1 and 6:1, memory dumps after each drop).

**The queue** is nine halfwords in the player context at **`+0x2374`** (`0x80112594` for player 1). Entry 0 is the next piece, entry 1 the one after, and so on; the original shows entries 0 to 2, the next piece at the bottom of its stack. When a piece is taken, `func_8008A2BC` copies entry 0 to **`+0x2386`** (the piece in hand, `0x801125A6`; −1 while nothing is held), shifts entries 1–8 down one and writes the new piece (its argument) into entry 8, so the game always knows eight pieces ahead. The same function shifts each slot's model rotation (16 bytes per slot from `+0x21D8`) and position (12 bytes per slot from `+0x2278`; the original's slots sit at x = −210 on screen), which is what makes the original's stack slide. Called from `func_8008A6F8`, `func_8008A758` and `func_8008E91C`. `func_800A3E6C(slot)` draws one slot's model.

**Entry values are indexes into the level's shape table**, 48 pointers at **`0x8014CBC8`**, set up for each level by `func_800906E8` (the loop at `0x80090E60`): every entry first points at a blank record (`0x801317C0`), then entries 0 to n−1 (n at `0x800E44B8`, the level's number of shapes: 2 at 1:1, 3 at 6:1) point at the level's shape records, and entries from 22 on point at fixed records. **22 is the "?" mystery piece** (writing 22 into the queue turns every slot into "?" pieces). So the same index means different shapes in different levels; the HUD should go by the record.

**Shape record** (as far as decoded):

| Offset | Size | Meaning | 1:1 square | 1:1 bar | 6:1 example |
| --- | --- | --- | --- | --- | --- |
| `+0` | s16 | Block value (material × 9, the value written to the board) | 297 | 288 | 324 |
| `+2` | s16 | Material, which sets the colour (33 blue, 32 yellow, 36, 37 seen) | 33 | 32 | 36 |
| `+4` | s16 | Number of cells | 4 | 3 | 4 |
| `+6`, `+7` | u8, u8 | Footprint size (probably width and height) | 2, 2 | 3, 3 | 2, 3 and 3, 2 |
| `+8` | u8 | **Shape ID**, the same in every level (1 the bar, 2 the square, 5 and 6 two four-cell shapes) | 2 | 1 | 5, 6 |
| `+0xC` on | s16s | Cell layout data, not yet decoded | | | |

For sprites, the shape ID (`+8`) picks the picture and the material (`+2`) its colour. Matched against the original's queue (Rescue 6:1, 2026-11-06): **1 the bar (3 cells), 2 the square, 5 the S (4 cells, 2 × 3), 6 the T (4 cells, 3 × 2)**. IDs 3 and 4 (probably the L shapes) are still to see. The layout data from +0xC is not yet decoded.

### The core opening a level needs (2026-11-05)

Found for the HUD's progress bar from the game's code (`func_800AB4C8`, `func_800AA3A4`, `func_8008B220`), then **confirmed in play**: painting Rescue 1:1's layer 1 a single colour (every cell 288, through the poke file) set off one combo that cleared it; halfway through, layer 1 had 500 empty cells (value 0), the exposed-core map had exactly 500 bits set, the won flag (`+0x3236`) was 1, and the game went on to 1:2.

**A Rescue level is won when an S × S square of the core is uncovered**, where **S is the player context's `+0x23E4`** (`0x80112604` for player 1): twice the level record's byte 12, kept between 2 and 10, so 4 to 8 in Rescue (4 at 1:1, 6 at 6:1 in the test runs). See [blockplanet-level-data.md](blockplanet-level-data.md).

- **The exposed-core map:** 32 words, one per board row, pointer at `0x800E44C8` (`0x801166F0` in play); bit *x* of row *y* is set while the core under column *x* is uncovered. Kept by `func_8008B220` (write a block: x, y, layer, value): when it writes **layer 1**, the layer just above the core, clearing the cell (value 0, as seen in play) sets the bit (`func_8008A880`, which also checks the core cell below) and a block clears it (`func_8008A83C`). So "uncovered" means layer 1's cell is empty.
- **The check:** after blocks are cleared (`func_800AB4C8`), if the level's goal type `0x800E44A4` is 3 (core opening; Hide & Seek's goals use other values), the game tries every square whose corner is at **even** coordinates (x and y = 0, 2, …, 30; squares wrap round the board's edges), counting with `func_800AA3A4(x, y, S, S)` the rows whose S bits from column x are all set. A square with S full rows wins: `func_800AC8E8` sets the won flag (context `+0x3236`) and blows open that square through layers 7 to 1.
- In layer 1 at the start, apart from the colours (types 32–37), about a fifth of the cells are type 1 (still unknown); the one-colour test also cleared them (they were painted over), so whether a normal combo can clear them is still open.

**For the HUD's progress bar** (weakest point, [HUD_DESIGN.md](../HUD_DESIGN.md)): use the same squares the game checks, S × S at even corners with wrapping. A square's "blocks left" is the number of filled cells above the core in its columns: non-zero cells in layers 1 to 8. The weakest point is the square with the fewest; its count at the level start is 100%. This reads the board each frame (1,024 cells × 8 layers, cheap), so it needs no hook. Still to check in play: whether type 1 cells can be cleared by normal play (if not, they should not count).

### Magic, combos and score (2026-11-05)

- **The stored magic is one byte, player context `+0x702`** (`0x80110922` for player 1): **−1 = no magic, 0 to 5 = the magic held.** When a combo ends (`func_800CD3B8`, at `0x800CE0BC`) with **20 or more pieces**, the game plays a sound and **adds 1** to it, capped at 5 (`func_800C78A0`). So the first 20+ combo gives magic 0, and each further 20+ combo while holding it **upgrades** it to the next type. Using it sets it back to −1 (seen in the Training lesson). Name cheats also set it (context `+0x1272` sets 3; `+0x1267`/`+0x1268` set 0 or 4).
- **The six magic types**, by writing 0 to 5 into `+0x702` during Rescue 1:1 (the model appears above the bowl; the game picks it from the table at `0x80103424`):

| Value | Model |
| --- | --- |
| 0 | A purple and yellow firework rocket |
| 1 | A bundle of red dynamite |
| 2 | Red claws (three prongs) on a dark base |
| 3 | Glowing purple orbs around a light centre (atom-like) |
| 4 | A white and red bomb or missile |
| 5 | A green and gold spinning top or drill |

  Likely names, from guides (Hard Drop, Breezewiki, the manual): 0 Firecracker, 1 Bundle O' Dynamite, 2 Electro-Magnet, 3 Atom (or Molecule), 4 Rocket, 5 Ray Gun; the order of 4 and 5 is not confirmed. What each one does to the board is still to record (use each and watch).
- **The magic meter** (the original's dot trail): player context **`+0x2AEC`** (u16), filled at **`+0x2AEE`**: **14** in most modes, **9** in modes 6 and 7 (VS), set at level start (`0x800AE9E4`). `func_800941B8(n)` adds *n*: **1 for each combo of 6 or more pieces** (`func_800CD3B8`) and **3** from two other places (`func_800C7FC4`, and again `func_800CD3B8`), which the guides say are fuse and gravity combos. When the meter reaches the limit it returns to 0, a sound plays and the held magic goes up by one, exactly as a 20+ combo does. Seen in the Training lesson: the 21-piece combo left the meter at 1 of 14.
- **Other ways to get magic, from the guides** (not yet seen in the code): 1×1 **magic crystals** hidden on the sphere, which give or upgrade magic when broken open; and **losing a life destroys the held magic**. In multiplayer only one magic (the Bomb) is available. Magic is used with C-down.
- **Combo counter:** **`+0x2AE0`** counts the pieces in the combo as they clear (`func_8009423C` adds 1 per piece). When the combo ends it is copied to `+0x2B14` (what the ring shows) and `+0x2AF0`, the best combo of the level goes to `+0x2C78`, and the running count returns to 0. Combos of 6 or more also call `func_800941B8`; 20 or more give magic.
- **Score:** **`+0x2AF8`** (u32, `0x80112D18`); the HUD shows it times ten.
- **Combos set off by writing to the board give no magic.** Painting blocks one colour clears them in one combo (768 cells with walls in every fourth row, so the level does not end), but `+0x702` stayed −1. Magic is only awarded where a drop's combo is resolved.
- **Correction to the HUD inventory:** the orb in a brass casing above the bowl is **the drop timer**, not the magic item. Its dial turns from blue to green to yellow while a piece is held, and a heart is lost when it runs out (seen with no input). The magic model appears just above the bowl.

**The held magic's model** (`func_800C78A0`, called from `func_800CD3B8` at `0x800CFF64`): drawn through the model helper `0x8002D93C` with an identity matrix (`0x80161540`) and a translation (`0x801309A8`, 0, 0, −2054); the model's own animation places it above the bowl. Measured by moving it: the camera is about 1,610 units from it (1 screen height ≈ 1,133 units there), and its own centre is at about (431, −331, 445) in its space. Drawn 3D outside a UI span, it is clipped at the original 4:3 edge on wide pictures. When used, the magic flies as a different model object (in a general object pool, e.g. `0x801EFC20` in Rescue 1:1) through the same translation matrix; the core's character (e.g. `0x801EEB80`) is drawn through it too.

**Research aid, the one-colour board:** a poke file setting every layer 1 cell (`0x801173F0` to `0x80117BEE` in Rescue 1:1; board pointer `0x800E44D0` + 2048) to 288 sets off one huge combo on the next frame and wins the level. With every fourth row set to 423 (type 47 walls) instead, the combo clears 768 cells and the level goes on. Only layer 1 (the top layer at the start) is drawn, so writing layer 2 shows nothing. Writing `+0x702` (0 to 5) gives a magic for testing.

### Where things happen on the board, for floating numbers (2026-11-05)

For the HUD's floating numbers (points, time and combo names rising from where they were earned):

- **The cursor** (where the next piece will drop): player context **`+0x2B2A`** (column) and **`+0x2B2E`** (row), board cells, one step per D-pad press, wrapping round the 32 × 32 board. The same pair repeats at `+0x2B32`/`+0x2B36`, and two more pairs at `+0x2B1A`/`+0x2B1E` and `+0x2B22`/`+0x2B26` (probably the piece's other corners or previous positions; not checked).
- **Where blocks cleared:** BlockPlanet can see this directly by comparing the board (`0x800E44D0`, 9 layers × 1,024 cells) from one frame to the next: cells that turn to 0 cleared in that frame. A combo clears over several frames, so the cells cleared between the combo counter (`+0x2AE0`) rising from 0 and returning to 0 are the combo; their centre is where the number rises from. BlockPlanet draws the sphere, so it can turn a cell into a screen position with the same mesh.
- **Points earned:** the change in the score (`+0x2AF8`, shown × 10) over the same frames.
- **Time Trial's clock:** player context **`+0x2B0C`** (s32), frames left at 60 per second (16,758 showed as 4:38 to 4:39; three seconds later 16,470). Time gained is a jump upwards; the HUD routine reads it at `0x8007611C`.
- **Combo names** (fuse, gravity): the magic meter's +3 calls (`func_800C7FC4` and one in `func_800CD3B8`) mark them; telling fuse from gravity still needs a look at those two places.

So the floating numbers need no hooks into the game: BlockPlanet reads the board, the combo counter, the score and the clock each frame.

### Hide & Seek goals and their pictures (2026-11-05)

For the HUD's goal progress. Levels started with `launch:hide_and_seek:0:L` and played past the goal card (the board is only built then).

**Goal number:** the level structure's `0x8013D248` (the record's group byte through the table at `0x800E4520`), copied to **`0x800E44A4`**. The card text is picked by `func_800890B8` (jump table `0x800F0EC4`) and the win check by `func_800AB4C8` (jump table `0x800F1068`):

| Goal | Name | Levels seen (stage 1) |
| --- | --- | --- |
| 1 | MULTI | 1, 6, 11 |
| 2 | DRILL | 2, 7 |
| 3 | Core opening (Rescue's rule; the card says HIDE AND SEEK) | 5, 10 |
| 4 | SHIFT | 9 |
| 5 | UNIQUE | 8 |
| 7 | BRICK | 3 |
| 9 | CONNECT | not yet seen |
| 10 | TOWER | 4 |
| 11 | CRYSTAL TOWER | not yet seen |

**Every goal is won by uncovering core:** each branch ends in `func_800AA3A4`, the count of fully uncovered rows in a rectangle of the exposed-core map (see the core opening above); only the rectangles differ.

**Where the pictures are:** in **layer 0**, plain core is type 9 (Hide & Seek) and **pictures are other types**:

| Goal | Pictures in layer 0 | Objects above |
| --- | --- | --- |
| MULTI | The whole core is type 8 in **3 × 3 tiles** (cell values 0–8 within each tile, tiles starting at x, y = 1, 4, 7, …): 100 small pictures, any one wins. A type 9 border runs round the board's edge | — |
| DRILL | One **5 × 5** picture, types 14–17 | Five drills: type 5 (the tip poking out, top layer) over type 2 (layers below). Only one drill has the picture under it |
| TOWER | One **5 × 5** picture, types 14–17 | A cross-shaped tower of type 31 standing on the picture through every layer; "uncover all the pieces around the tower" |
| SHIFT | Four **2 × 2** parts, types 10–13 | A drill over each part; the parts are then slid together |
| UNIQUE | One shape of 32 cells, types 18–25 | One drill |
| BRICK | None (all plain core) | Five red **bricks** (type 38, three cells each, L-shaped) resting in **layer 3**; one must fall to the core |

Type 7 (two per playfield layer, also in Rescue from 1:2) is probably the guides' 1×1 **magic crystal**; not confirmed.

**For the HUD** ([HUD_DESIGN.md](../HUD_DESIGN.md#hide--seek)): picture cells are the layer 0 cells that are not plain core (types 10–25; in MULTI, each 3 × 3 tile). Blocks above a picture are the non-zero cells in layers 1–8 over its cells, not counting drill (2, 5) and tower (31) blocks, which the player does not have to remove. A brick's height is the layer it is in; it has reached the core at layer 1.

### The title screen's artwork (2026-11-06)

Found for a sharper title on large screens. The title screen is mostly 2D: the Nintendo 64 logo and the intro are 3D models, but the title's logo, character art, oval and copyright lines are **one 270 × 205 image** (16-bit RGBA5551, 1-bit transparency), drawn by the image helper `0x8003396C` at (24, 21) in the 320 × 240 frame (`0x8019D250` in the sample, a header of width, height and format 0, then the pixels), over a 280 × 188 one-byte-per-pixel soft shadow (`0x801B82D0`, format 2). Image headers: s16 width, s16 height, s16 format (0 RGBA16, 2 one byte per pixel), then the pixel data from +8. Logged with `BLOCKPLANET_DRAW_LOG=1` (every distinct 2D draw, its image and position).

**Redraw test** (private files only, generated from the ROM's image): tracing the 1-bit outline into smooth vectors (potrace) and upscaling the painted inside smoothly gives sharp outer edges at any size with the painting softened but intact; tracing each colour area as well makes the letters' inner edges sharp too but flattens the shading into bands. Small details (the ™) are lost by the trace and need care. Extruding the letters into 3D is not practical: the logo's bevels, sides and shadow are painted into the picture.

**Rebuilding the title from models (2026-11-06, in progress):** the intro is run by `func_80067798` (it draws three LightWave scenes, pointers at `0x800E166C`, `0x800E1670`, `0x800E1674`). **The 3D letters are the scene `0x801E4E10`** (81 objects; hiding it removes the letters entirely): **each orange letter is its own object, 11 to 21: T 15, E 13, T 14, R 12, I 11, S 16, P 18, H 19, E 21, R 17, E 20** (in the word's order); objects 0–10 draw nothing visible (probably parents, camera and lights), 22 onwards are the coloured sparkles, the icy shells that shatter in 40–59, and the shards in 60–80. `0x80204AB0` (112 objects) is the earlier part of the intro (robots and blocks); `0x801EED80` is the grid sphere behind the title. The title image (`0x8019D250`) can be switched off cleanly (`BLOCKPLANET_IMAGE_SKIP=8019D250`, any 2D image, anywhere), leaving the grid sphere background and the soft shadow image (`0x801B82D0`). **On the title** (`func_800685FC`) the letters scene stays in memory (geometry untouched, animation at its end frame, 180). The title routine itself draws the scene at `0x800E1670` until its animation ends (call at `0x80069460`), then clears the pointer; the intro routine is `func_80067798`. Experiment `BLOCKPLANET_TITLE_LETTERS=1` (`src/graphics/title_hooks.cpp`) puts the pointer back each title frame, restores a snapshot of the scene's memory taken at a chosen frame (`BLOCKPLANET_TITLE_FRAME`, default 160) and filters to the letters: the call runs, but nothing is drawn yet, even unfiltered. **What works instead: holding the title's own animation.** `BLOCKPLANET_TITLE_FREEZE=frame` (hook before the title's call at `0x80069460`; a1 there is the frames elapsed, `0x800E1208` − `0x800E1210`) sets the scene's frame (+0x24) so that after advancing it lands on that frame, so the animation never ends and the game keeps drawing the 3D letters. Frame **150** shows them fully formed and opaque; by 165 they are fading into the picture. The title picture's fade-in is tied to the same animation, so a second hook (before `0x8006923C`) draws it fully opaque (its alpha argument at sp+0x1C). **Face-on letters (experiment, `BLOCKPLANET_TITLE_FACE=1`, off):** each letter is drawn as the scene's camera matrix (scene +0xF8, double-buffered by +0x3D) times its own matrix (objects at scene +0x14, 0x120 bytes each, two 64-byte N64 matrices; the letters' rotations are about 20° off square, scale about 0.07, translations up to ±170 in several depths). Replacing each letter's rotation with the camera's inverse turns it face-on, but the letters drift (the Es drop well below the word): their geometry sits far from their pivots. The three floats at each letter's parts data +0x10 (e.g. 37, 39, 15 for an E) are not that offset. Next: find the letters' vertex data (the texture and display-list setup is in `func_80032320`) to get each letter's true centre, or measure the drift on screen and solve for it.

**Now the default (2026-11-06):** the title shows the 3D letters held at frame 150 over the grid sphere, and the painted picture (logo, characters, ring, rockets and the copyright lines; image pointer `0x800E1614`) and its soft shadow (`0x800E1638`) are not drawn until they are replaced. `BLOCKPLANET_TITLE_FREEZE=-1` restores the original title. Start, the menus and play are unaffected.

Objects can be shown alone with `BLOCKPLANET_OBJECT_ONLY=scene:i,j` (hook on `func_800321D4`, the per-object draw in the LightWave player; its a3 is the object index). `BLOCKPLANET_FN_PROBE=1` logs when candidate scene routines run.

**The copyright lines are not text in the game:** the credits list (`0x800EBF00` to `0x800ECC00` in memory, names and roles) does not contain them, and neither does the ROM as plain text; they exist only painted in the title image. Replacing them with text would mean reading them from that image at install (the font is a clean pixel font, so matching characters is reliable) rather than writing them into the repository.

### The game's floating number (2026-11-06, in progress)

After a combo the game shows a large grey 3D number at the sphere's centre for about a second ("6000" for a combo worth about 59,000 points, so it is not the total; probably a bonus). It is not one of the 2D helpers' draws, and no new model appears for it in the model log. Skipping the scene `0x801EEB80` (`BLOCKPLANET_MODEL_SKIP`) stops combos from playing out at all (the game waits for that scene's animation), so the number is probably one of its objects; finding which (with `BLOCKPLANET_OBJECT_ONLY`, as for the title letters) is the next step, to hide it in Story and to reuse it in other modes. `0x801E3B00` is the tunnel background.

## The original Time Trial's clock and bonuses (2026-11-06)

Read from the generated code while moving Score Attack onto Story's stages:

- **Start:** `0x800AE950` sets the clock (`+0x2B0C`) to 18,059 (0x468B, frames: about 5:01) when mode 2 starts.
- **Each frame:** `0x800CED54` subtracts the frame step (`0x80160C60`) in mode 2; modes 4, 5 and 7 add it instead (`0x800CEC38`, a count-up timer). In mode 1 (Rescue) the clock is not touched.
- **Time out:** at 0 in mode 2, `func_800AA4B0(0)` is called and the clock set to 0x4F1A00 with a flag at `0x8013FAD4` (`0x800CEDBC`).
- **Game over by hearts:** `0x800CEDE8`: hearts (`+0x2D7E`) at 0 become -1, the game over. BlockPlanet's Score Attack ends a run by setting hearts to 0 (checked: the game over and the scores screen follow).
- **Sphere bonus:** `func_800C7C90` (mode 2 only, clock above 0, once per sphere): `func_800C7AF8(n)` is 1 for the first sphere and doubles each sphere (2, 4, 8...), capped at 32; the score gains that × 100 × the multiplier (`+0x2AE6` + `+0x2AE8` + 1), shown × 10, and the same value goes to the floating number routine `func_800B363C`.
- **Time gained by combos:** no other store to `+0x2B0C` was found, so how combos add time is still open (perhaps through the frame step or a copy of the context).
