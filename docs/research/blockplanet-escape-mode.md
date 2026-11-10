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
- **Gravity:** not yet found. It needs a block left unsupported (a poke is planned).

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

**Estimate:**
- **Built on the game's engine:** large overall, about as large as the roadmap expected. The view is the easy part, and is mostly shown now. The landing reversal and the shell boards are the core of it. Medium to high risk, because landing, gravity and matching share the board helpers' assumptions (towards the core is down).
- **The roadmap's long-term path:** BlockPlanet's own engine, the same one network play and a fast mode need, would carry Escape without these risks.
- **Suggested next step, if the owner wants to go on:** the three write watches above. They decide whether landing and the miss test are each in one place. If they are, a playable Escape on the game's engine is a medium job; if not, it waits for BlockPlanet's engine.
