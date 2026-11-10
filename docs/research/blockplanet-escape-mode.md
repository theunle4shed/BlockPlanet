# Escape mode (research, started 2026-11-10)

The owner's idea (`docs/ROADMAP.md`, New game modes):
- The player and the camera start inside the sphere, and no core layer is visible.
- The player drops blocks onto the underside of the existing blocks.
- Orphaned blocks can still drop to the core with gravity.
- A block that misses its target can't stay under the core line: it dissolves and costs a life.

Two steps, each reported before going further. Step 1 is a visual prototype; step 2 is the rules, as notes only.

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

These three would come from write watches on the real game, as the pause flag was found:
1. **The landing routine:** a write watch on the block values under the cursor's column during a drop. It names the function that searches the column and writes the piece's cells, and gives the layer it picks.
2. **Where a drop loses a heart:** a write watch on `+0x2D7E` during a drop that loses one. It names the "miss" test: what makes a drop a miss today (no matching shape touched, or the piece not fitting, to confirm).
3. **Gravity:** writes to the block values during a gravity combo. That names the routine that moves unsupported blocks down.

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
| Game over and goals | Escape's goal is to be decided (escape through the shell? clear it?). The core-opening goal doesn't fit | Small (once decided) | Low |

**Estimate:**
- **Built on the game's engine:** large overall, about as large as the roadmap expected. The view is the easy part, and is mostly shown now. The landing reversal and the shell boards are the core of it. Medium to high risk, because landing, gravity and matching share the board helpers' assumptions (towards the core is down).
- **The roadmap's long-term path:** BlockPlanet's own engine, the same one network play and a fast mode need, would carry Escape without these risks.
- **Suggested next step, if the owner wants to go on:** the three write watches above. They decide whether landing and the miss test are each in one place. If they are, a playable Escape on the game's engine is a medium job; if not, it waits for BlockPlanet's engine.
