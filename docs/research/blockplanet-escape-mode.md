# Escape mode (research, started 2026-11-10)

The owner's idea (`docs/ROADMAP.md`, New game modes):
- The player and the camera start inside the sphere, and no core layer is visible.
- The player drops blocks onto the underside of the existing blocks.
- Orphaned blocks can still drop to the core with gravity.
- A block that misses its target can't stay under the core line: it dissolves and costs a life.

Two steps, each reported before going further. Step 1 is a visual prototype; step 2 is the rules, as notes only.

**The goal (the owner, 2026-11-10):** "goal is for us to escape. Gravity still makes bricks fall towards the core.. it's just we are throwing bricks up from the underside. So 3×3 or 5×5 or 7×7 square to escape, based on level."
- **Win:** open a square hole right through the shell, through every layer to the outside: 3 × 3, 5 × 5 or 7 × 7, set per level. It is the inside-out version of the original's win, a core-sized opening.
- **Pieces** are thrown upwards from the core's side onto the shell's underside, and land just under the first block.
- **Gravity** stays inwards: unsupported blocks fall towards the core, and blocks that reach the core line dissolve.
- **A miss** (nothing above within reach) dissolves and costs a heart.
- **Matching and combos** work as normal.

## Step 1: the view from inside (prototype, built and checked 2026-11-10)

`BLOCKPLANET_ESCAPE_VIEW=1`, off by default; `BLOCKPLANET_ESCAPE_FOV` sets the view's width (100° by default). The rules don't change.

**How it is drawn.**
- The game models the bricks' undersides. Its sphere code has an inner (core-facing) face routine, `func_8009C6D4`: the outer face's quad on the block's inner corners, wound the other way, with its shading negated and the block's own texture coordinates. The game never calls it; its quadrant passes draw only the outer faces and the sides.
- The prototype draws the game's own board in place of the sphere's usual drawing (`draw_inside`, `src/graphics/sphere_quadrants.cpp`). For each of the patch's 15 × 15 cells it runs, in order:
  - the game's cell setup;
  - a move to the column's first block above the core (layer 1);
  - the game's per-block setup (`func_8009E314`, `func_8009E214`);
  - the inner-face routine.
- So the bricks are the board's real ones: their texture, shapes, filler blocks and colours.
- The core is not drawn, and a cell dug down to it is open.
- The camera (`src/graphics/escape_view.cpp`) sits at the sphere's centre, looking out at the patch's middle with up along its rows. It has its own perspective, pushed before and popped after, and then the game's projection is loaded again.

**Checked on the real game** (Story 1:1, the parent session):
- **Works:**
  - The board's real bricks are seen from below, textured, with Story 1:1's yellow and blue, their shapes, the filler blocks and the magic crates' purple faces.
  - The shading reads well; it isn't dark.
  - The core is never drawn.
  - At 150° the patch wraps round into a tunnel with a vanishing point, and the game's tunnel background shows at the far end. At 100° the view is a flat close-up.
- **Broken (expected for a prototype):**
  1. **Turning barely shows.** The view is locked to the patch's middle, and the board moves under it (the mesh is rebuilt as the sphere turns), so only the bricks shift.
  2. **No cursor, falling piece or landing marker.** They are drawn from the outside camera and hidden behind the shell. Two drops lost two hearts, as misses: there was nothing to aim with.
  3. **A large pale lavender quad** covered the lower screen after the second drop. It is probably the dropped or landing piece, or the landing marker, drawn near the camera in our view's space.
  4. **No depth:** at 150° the patch's far edges overlap at the corners.

**The camera, after the first run** (the owner, 2026-11-10: "the camera would need to be as far back inside the core as it can and facing the brick opposite"):
- It is pulled back from the centre towards the far side's inner surface, still looking across the sphere at the patch. `BLOCKPLANET_ESCAPE_DISTANCE`: 0 is the centre and 1 the far side; 0.85 by default.
- A moderate view, 70° by default (`BLOCKPLANET_ESCAPE_FOV`).
- That frames the patch much as the outside view does, flattening the curvature, with its rim past the screen's edges, as the outside view's is.
- Only the patch facing the camera is drawn (the game's 15 × 15), so the far side behind the camera is never in the way.

**Drawing the cursor, falling piece and landing marker from inside.** Everything the game places on the sphere reads the mesh table or the cell setup's work area: `func_80097E3C`, `func_80098D8C`, `func_800A0ED8`, `func_800A2374`, `func_800A18F4` and `func_800A1E58`. The last two call the face functions (`docs/research/blockplanet-game-state.md`, sphere proportions). So:
- **The landing marker and the piece on the sphere:** wrap those routines' calls in the inside camera (`escape_view::begin` and `end`, as the board is wrapped now), and draw their inner faces rather than their outer faces where they draw faces. First, find which routine draws what: a draw log of those six while a piece is held, then dropped.
- **The falling piece in flight:** a model, drawn through the model helper (`0x8002D93C`) with the game camera's matrices. It needs its matrix replaced by one in the inside camera's space, placed by its own position along its fall (its height above the board, from the player context), as BlockPlanet already does for the held magic (`src/graphics/magic_hooks.cpp`).
- **The lavender quad:** probably one of the routines above drawing after our pop, with the game's matrices but at a point that is now near our camera. The same draw log will name it.
- **Depth:** the board, the pieces and the marker would then need the depth buffer (the game's own z modes) rather than our no-depth shell.

Effort: medium (a few hooks, one matrix rebuilt per frame). Risk: medium, mostly in naming the routines and their order.

## Step 2: the rules (notes only, 2026-11-10)

### What the game does now (known)

- **The board:** 9 layers (0 the core, 1 to 8), each 32 × 32 cells. The tables are block values (`0x800E44D0`, 2 bytes a cell), face masks (`0x800E44D4`), a 4-byte entry (`0x800E44D8`) and the column map (`0x800E44DC`). In the column map the high nibble is the lowest exposed layer and the low nibble the highest (`func_8008B0E0`). Value ÷ 9 is the block type.
- **Board helpers:** 42 functions (`0x8008A7A0` to `0x8008CA34`), called from about 70 functions: generation, gravity, matching, explosions and magic, landing, game over and scoring. The 1,024-cell layer size, the 9-layer bounds and the 4-bit layer numbers are built into them.
- **The cursor:** player context `+0x2B2A` (column) and `+0x2B2E` (row). `0x8015F88A` and `0x8015F88E` hold the origin of a 13 × 13 neighbourhood that the drop and shadow logic use.
- **The drop timer:** `+0x2B10`, counted in `func_800CD3B8`, the per-player update. A heart is lost when it runs out.
- **Hearts:** `+0x2D7E`. At 0 they become −1 at `0x800CEDE8`, the game over. Losing a life also destroys the held magic.
- **Landing:** a dropped piece falls down its columns, towards the core, and rests on the highest occupied layer. Pieces also land on Puzzle's type-47 walls.
- **Gravity:** after a clear, unsupported blocks fall inwards (gravity combos).

### Not yet found (what step 2 still needs from the game)

These three come from write watches on the real game, as the pause flag was found (the first two are found, below):
1. **The landing routine:** a write watch on the block values under the cursor's column during a drop. It names the function that searches the column and writes the piece's cells, and gives the layer it picks.
2. **Where a drop loses a heart:** a write watch on `+0x2D7E` during a drop that loses one. It names the "miss" test: what makes a drop a miss today (no matching shape touched, or the piece not fitting, to confirm).
3. **Gravity:** writes to the block values during a gravity combo. That names the routine that moves unsupported blocks down.

**Research aid:** `BLOCKPLANET_BOARD_LOG=1` logs, during play, each board cell that changes each frame: layer, x, y, and the value before and after (`board` events). It also logs the cursor and hearts when they change (`board_player`). Paired with a debugger's write watch for the writing function, it shows a drop's cells and layer, the heart a miss costs, and a gravity combo's moves, frame by frame.

### Found with the watches (2026-11-10, Story 1:1, `BLOCKPLANET_BOARD_LOG` and debugger write watches)

The planes' base in 1:1 is `0x80116BF0`.
- **The piece is stamped into the board every frame while held.** `func_800CB1E8` (from `func_8009AB60` ← `func_800A348C` ← `func_800CD3B8` ← `func_80070118`) writes the hovering piece into the board at its target layer with `func_8008BE58` (through `func_8008BB24`), then removes it with `func_8008CA34` (through `func_8008CE04`). A write watch on one cell fired every frame, alternating the piece's value and 0, 229 times in about 60 s. This is likely for the landing marker or the drawing. **The landing layer is chosen in `func_800CB1E8` or `func_8008BB24`, before the stamp**, so that is where Escape's reversed landing would go: there, the search runs from the core outwards and the piece goes below the first block. A real landing shows in the log as a stamp that is not removed: the test's drops stacked at layers 2, 3 and then 4.
- **Hearts are lost in `func_800940F0`**, from `func_8009423C` ← `func_800CD3B8`: 3 → 2 → 1 → 0. The final 0 → −1, the game over, is in `func_800CD3B8` itself. In 1:1 every test drop cost a heart while its piece still landed, so this is the game's penalty for a drop that matches nothing, not a miss: today a piece always lands on something. **Escape's miss test is therefore new**: a drop with no block above it, inside the reversed landing search. It would call the same heart loss (`func_8009423C`, or `func_800940F0` directly), and dissolve the piece instead of stamping it.
- **Gravity: not yet found.** A block poked to float in Story 1:1 (layer 3 over empty layers 1 and 2, at x 10, y 10) did not fall. About 4 s later, after a drop, `func_8008CA34` removed it, called from `func_800CC0D0` ← `func_800CC408` ← `func_800CD3B8`. That is **not gravity but the board's piece checker**, already known from Training's stripes (`docs/TRAINING_REDESIGN.md`):
  - every frame it checks 5 cells on layers 1 to 7;
  - it finds each cell's piece through its shape table (`func_80093214`, `0x8014CC98`) and checks that every cell of the shape is still there (`func_800924E0`);
  - it removes pieces that aren't whole.

  The poked block was a lone cell of a four-cell piece, so it was removed as broken; support had nothing to do with it. Gravity proper (a whole piece left hanging after the blocks under it clear, the game's "gravity combos") still needs a real run: dig out the blocks under an intact piece with a matching drop, then watch the cell below it. The two board changes during the combo (removed at layer L, written at L − 1) will name the routine.

### What Escape would change

In board terms:
- The shell is the blocks.
- The player drops from the core's side, so a piece rises up its columns, away from the core, and stops just below the first block it meets: the lowest occupied layer minus one.
- Gravity stays as it is (the owner's rule: orphaned blocks still drop to the core).
- A miss is a drop with nothing to stop it: no block above in any of its columns, within the board's 8 layers. The piece dissolves and a heart is lost.
- Nothing may rest under the core line. With no core shown, a block that falls all the way to layer 1 has reached it, and dissolves (the owner's "can't stay under the core line").

What each part takes:

| Part | What it takes | Size | Risk |
| --- | --- | --- | --- |
| Boards built as a shell | Escape's stages need their blocks in the outer layers, with room below for pieces to rise into. Either the level builder (`func_800857E8`) gets an Escape fill, or BlockPlanet writes the board at level start (as the poke tests did) | Medium | Low to medium: the board's other tables (face masks, column map) must be rebuilt to match, through the game's own helpers |
| Landing reversed | Replace the landing search: from the core outwards to the first occupied layer, the piece placed below it. One function if the landing is in one place, as the drop and shadow neighbourhood suggests | Medium | Medium: the shadow (landing marker) has its own search that must agree |
| Miss and dissolve | At the miss test: "no block above" instead of today's rule; then the game's own heart loss | Small to medium | Low once found |
| Gravity | Unchanged (inwards), plus "dissolve at the core line": blocks reaching layer 1 with nothing under them are removed (and perhaps cost a life; the owner's call) | Small to medium | Medium: gravity runs inside combos, so removals must not break combo counting |
| Matching and combos | Unchanged, in board terms: shapes still match by type, and only the direction pieces arrive from changes | None | Low |
| Inside view | Step 1 plus the cursor, pieces and marker (above). The underside drawn is the lowest block per column, not layer 1, once boards are shells | Medium | Medium |
| The goal: an S × S hole through the shell | **Very likely the game's own core-opening check, unchanged.** That check (`func_800AB4C8` after a clear, when the goal type `0x800E44A4` is 3) tries every S × S square at even corners, wrapping, with S the level's opening size (player context `+0x23E4`: twice the level record's byte 12, so even sizes from 2 to 10 today; Escape's 3, 5 and 7 would be new, odd values). It counts uncovered rows with `func_800AA3A4` on the exposed-core map (`0x800E44C8`, a bit per column set while the core under it is uncovered, that is with no block above it in any layer). In a shell board a column is uncovered exactly when a hole runs through every layer, so the same check is the escape. To confirm: whether odd sizes work with the even-corner search (a 3 × 3 hole may need odd corners too), and that the map is rebuilt from all layers rather than from the lowest one | Small | Low to medium |

**Estimate, after the watches (2026-11-10):**
- **Landing is in one place** (`func_800CB1E8` / `func_8008BB24` choose the layer, then `func_8008BE58` stamps the piece). A reversed landing (searching from the core outwards, the piece placed under the first block) and the new miss test (nothing above: dissolve and call the heart loss, `func_8009423C`) both go there. **Medium.**
- **The heart loss is one call**, reused as it is. **Small.**
- **The win** is very likely the game's own core-opening check (above), plus odd sizes. **Small to medium.**
- **Shell boards** at level start, through the game's own board helpers so the masks and the column map agree. **Medium.**
- **The inside view**, with the cursor, piece and marker drawn from inside. **Medium.**
- **Gravity:** unknown until found. If it is one routine moving whole pieces down a layer, Escape keeps it as it is (inwards) and adds a dissolve at the core line: small to medium. If it is spread through the combo code, it is larger.
- **The piece checker** (`func_800CC0D0`) works on whole pieces regardless of direction, so it should be unaffected.
- **Overall: medium to large, with medium risk**, down from "large, medium to high". It is feasible on the game's engine as long as gravity is found in one place. Next for that: the gravity run above.

**Estimate:**
- **Built on the game's engine:** large overall, about as large as the roadmap expected. The view is the easy part, and is mostly shown now. The landing reversal and the shell boards are the core of it. Medium to high risk, because landing, gravity and matching share the board helpers' assumptions (towards the core is down).
- **The roadmap's long-term path:** BlockPlanet's own engine, the same one network play and a fast mode need, would carry Escape without these risks.
- **Suggested next step, if the owner wants to go on:** the three write watches above. They decide whether landing and the miss test are each in one place. If they are, a playable Escape on the game's engine is a medium job; if not, it waits for BlockPlanet's engine.

## Step 3: the game's code, read statically (2026-11-10)

Read from the generated code by the parent session, and given here as addresses and descriptions only. Each point names where an Escape change would go.

### Board primitives

| Function | What it does |
| --- | --- |
| `func_8008A7A0(x, y, L)` | Reads a block value |
| `func_8008A9C4(x, y)` | The column map's low nibble: the highest layer with an exposed face, 0 if the column is empty |
| `func_80092A14(piece, x, y, L)` | Counts empty cells at layer L; the piece fits if the count is at least piece + 4 |
| `func_800924E0` | Tests whether a piece is present at L |
| `func_8008DBF8` | Tests whether any of a piece's cells at L is occupied |
| `func_8008BB24` | Stamps a piece (through `func_8008BE58`); at L = 1 it clears the column's exposed bit |
| `func_8008CE04` | Removes a piece (through `func_8008CA34`); at L = 1 it sets the exposed bit |
| `func_8008DE44` | Stamps if nothing collides |

**Piece descriptors:** a 3 × 3 mask of halfwords at `+0xA + 6·dx + 2·dy`, with the base at `+0`, the type at `+2` and the cell count at `+4`. The tables are at `0x8014CBC8` (drops) and `0x8014CC98`.

### Landing

- **The layer is chosen by `func_8008AB0C(piece, x, y)`:** the highest column top (`func_8008A9C4`) under any of the piece's mask cells, plus 1. That is one rigid layer for the whole piece.
- **`func_8008AA00`** is its twin on the screen's local grid (it scans the planes from 8 down to 1 through `func_8008A910`). Only the landing marker uses it.
- **`func_800CB1E8` is a "would this drop score?" preview**, called only from the marker routine `func_8009AB60`. In order:
  - it picks the layer with `func_8008AB0C` and checks it is within 1 to 7;
  - it checks the piece fits there;
  - it stamps the piece, runs the match evaluator `func_800C84C8` against the threshold at context `+0x25C2`, and removes the piece again;
  - it returns 4 or 1, which go to context `+0x323C` and byte `0x8015D76A`.

  That is the every-frame stamp the watches saw. `func_8009AB60` stores `func_8008AA00` − 1 at context `+0x2444` (the marker's height).
- **The drop itself:**
  - `func_800ACB94` (input) sets the piece id at `+0x24AA`, the position at `+0x24AC` and `+0x24AE` (the cursor + 7, wrapped to 31), `+0x24B0` = 8, and the flags at `+0x24B2` = `0x5595`.
  - In `func_800CD3B8`, at `0x800CDA5C`, it takes a slot: one of 64 at context `+0x25CC`, 20 bytes each (`func_800921A8`). At `0x800CDC30` it sets `+0x24B0` to `func_8008AB0C` − 1 and starts the visual (`func_800C0308`).
  - The slot processor `func_8009423C`, with flag 1, sets slot `+0xE` to `func_8008AB0C` − 1 again (`0x80094450`).
  - With flag 4 it stamps at `+0xE` + 1 and runs the match. Below the threshold it removes the piece (`0x80094BB8`), plays the dissolve effect `func_800BCF78`, sets context `+0x1C4` = 1 and calls the heart loss `func_800940F0` (`0x80094BF4`). So a no-match drop dissolves, which still needs confirming in play (check 1 below).
  - The real drop recomputes the layer and never reuses `+0x2444`.

**Escape's reversed landing.** Replace `func_8008AB0C` and `func_8008AA00` behind a switch:
- For each of the piece's mask cells, scan layers 1 to N in the planes (not the column map) for the first non-empty one, and keep the lowest found.
- If there is none, it is a **miss**. If the lowest is 1, the drop is **blocked** (no room under the shell there). Otherwise return the lowest − 1.
- The callers keep their own −1 and +1.
- The miss goes in `func_800CD3B8` after `0x800CDC30`, and defensively at `0x80094450`. There are two precedents for "no piece, lose a heart": `0x800CDB40`–`0x800CDB8C` (through `func_8008A6E4` to `func_800940F0`), and `0x80094BA4`–`0x80094BF4` (remove, dissolve effect, heart).

**The catch:** a piece pressed under the shell has nothing below it, so by the game's support rule (gravity, below) it is unsupported and would fall straight back. Escape needs support redefined as "touching a block above or below", or the shell treated as fixed.

### Gravity (both halves are in one place each)

- **The trigger, `func_800AF424(piece, x, y, L, timer, arg)`,** runs after a removal. It is called:
  - from the slot processor (`0x800948F4`, `0x80094E0C`, `0x80095188`);
  - from the magic and explosion routines (`func_800B4A54` five times, `func_800B4740`, `func_800BC850`, `func_800BD418`, `func_800C2848`, `func_800C4160`, `func_800C5668`);
  - from `func_800954FC`.

  For each now-empty cell of the removed piece, it finds the piece above at L + 1 (`func_80093214`). If `func_8008DBF8(piece, L − 1)` collides, that piece is supported; otherwise it gets a new slot with flags `0x408` (falling) and a timer of arg + 15.
- **The support rule:** any of the piece's cells with a block directly below it, or the core.
- **The step:** in the slot processor with flag 8 (`0x80094C1C`–`0x80094E2C`), every 10 ticks. If the piece fits at L − 1 and L ≥ 2, it is stamped at L − 1 (`func_8008D90C`) and removed at L, the trigger cascades, and slot `+0xE` = L − 1. Otherwise (`0x80094E30`) it has landed: match and combo (`func_800C84C8` at `0x80094F94` and `0x80095008`).
- `func_800CB7E8` also starts falling slots (flags `0x428`, from layer 7).
- **For Escape:**
  - **The support test** in `func_800AF424` becomes "a block below, or a block above" (or "connected to the shell"), so that pieces stuck under the shell stay.
  - **Dissolving at the core line** goes at `0x80094E30` when L = 1 (the guard is at `0x80094D2C`). There is a precedent: goal 7's branch (`0x80094E3C`–`0x80094EFC`) calls `func_800ADC44` when a special type reaches a layer below 2.

### The win: the square check (`func_800AB4C8`, goal 3, its tail at `0x800AC554`)

- It loops the corners x0 and y0 over 0 to 30 in steps of 2. The step is a fixed immediate at `0x800AC5C8` and `0x800AC5E0`.
- `func_800AA3A4(x0, y0, S, S)` takes the mask (1 << S) − 1 from the table at `0x80164F5C`, rotated by x0 (`func_800AA138`, which wraps). It counts the rows (y0 + k) & 31 whose word in the exposed-core map `0x800E44C8` has every bit of the mask set. A count of S is a win (`func_800AC8E8`, `func_800AA4B0`).
- **S:**
  - `func_80088914` sets context `+0x23E2` to the record's byte at `0x8013D279` (the tool's byte 12), plus a term in mode 6.
  - `+0x23E4` is then twice that, clamped to 2..10.
  - S of 4, 6 or 8 also sets `0x8015F4F0` to 0, 1 or 2; other values leave it unset.
- **Odd sizes (3, 5, 7):** the mask and the row count already work. The corner step needs to be 1 at both addresses, `+0x23E4` set directly, and `0x8015F4F0` given a value.
- **But the map tracks only layer 1.** Its bits clear when something is stamped at layer 1, and set when layer 1 empties, and only over a core tile. A shell board, with layer 1 empty, would be won at once. Escape must rebuild the map before the check, with a bit set exactly when `func_8008A9C4(x, y)` is 0: nothing in the column, which is a hole through the shell.

### The starting board

- `func_800857E8` only builds the 192 piece descriptors.
- **The board is filled by `func_800906E8`**, called from `func_800CC6E0` at `0x800CC8F8`:
  - First, `func_8008EEA4(0x8013D170, …)` lays the core (layer 0) and the per-goal layouts (jump table `0x800F0F3C`).
  - Then the specials for layers 1 to N − 1 (`0x8009141C`).
  - Then the main fill (`0x80091498`–`0x80091684`). For layers 1 to N (N at `0x8013D17C`, the record's byte 0) it places T random pieces through `func_8008DE44`, removing pieces that already match (`func_800C8E24`, when `0x800DFD4C` is 2). It then puts filler in every remaining cell, and finishes with `func_8008C520` for each column.
  - Placement checks collision only, never support.
- **For a shell:** start the fill loop at K instead of 1 (the start at `0x80091498`, the bound at `0x8009167C`), so layers K to 7 are filled. Or hook after `0x800CC900` and clear layers 1 to K − 1 with `func_8008CE04`, then `func_8008C520`. Either way, the map rule above is needed.

### The draw routines (for the inside view)

`func_800A348C` runs these in order:
1. `func_80097CEC`, then `func_800A2374`;
2. the quadrant passes;
3. `func_8009FA20` (if `0x8015D770` is set);
4. `func_800A0ED8` (always);
5. `func_800A18F4` (if byte `0x8015FDEE` is set);
6. `func_800A1E58` (if byte `0x80160C12` is set);
7. `func_8009AB60(0x8015F848)`.

What each draws:
- **`func_8009AB60` is the cursor and shadow, the landing marker and the held piece's anchor: the one to wrap in the inside camera.** Its marker cells use `func_8008E5B0` (the column top). Its helpers are `func_80099F98`, `func_8009A044` (faces through `func_8009C330`) and `func_8009A348`.
- `func_80097E3C` sets up one cell's or one layer's corners; `func_8009AB60` and the magic effects use it.
- **`func_80098D8C` maps a board point to a 3D position** for things above the sphere. The falling piece uses it (`func_800C090C`, `func_800C0EE8`, drawn from `func_800CD3B8` after `func_800A348C`), and so do particles. **This is the likely lavender quad.**
- `func_800A0ED8`: a per-cell overlay pass.
- `func_800A18F4`: deferred type-63 bricks (queued by `func_80097848`).
- `func_800A1E58`: bricks queued by magic (`func_80096488`).

### Checks still open on the real game

1. A no-match drop: does the piece stay or dissolve? Break at `0x80094B4C` against `0x80094BF4`.
2. Gravity: break at `0x800AF7F8` and `0x80094DC8` during a real gravity combo.
3. Odd S: poke `+0x23E4` = 3, then patch the corner steps.
4. A shell board: poke the fill loop's start.
5. A draw log that skips one routine at a time, with a piece held and dropped.

### What it adds up to

Every part of Escape now has a known place in the game's code. The new pieces are:
- the reversed landing and the miss, in two functions;
- the support rule, in one;
- the dissolve at the core line, at one spot;
- the win map rebuilt, and odd sizes allowed;
- the shell fill, in one loop;
- the inside view of the marker and the falling piece, in two routines.

**The estimate holds at medium to large, with medium risk**, now with no unknown part. The one design point is the support rule (a piece under the shell must count as supported). The owner might like to weigh in on it.
