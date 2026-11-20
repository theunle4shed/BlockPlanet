# Score and magic

Started 2026-11-19 (the owner's request: the score formula and what each magic does), by reading the generated code and logging play (`BLOCKPLANET_SCORE_LOG=1`). Addresses are player 1's context (`0x80110220`). The score is kept in units of 10: the HUD and the game show it × 10.

## The score

| Value | Where | What it is |
| --- | --- | --- |
| Score | `+0x2AF8` (u32) | The total, in units of 10 |
| Combo points | `+0x2AFC` (u32) | The combo running now; copied to `+0x2B00` |
| Second total | `+0x2B04` (u32) | Gains the same points for bricks whose record has `+0x12` set; paid out by `func_800C7BE4` (purpose not yet known) |
| Combo steps | `+0x24BC` (s16) | Rises by one for each broken brick flagged `0x40` (`func_80093C08`); 0 in every ordinary combo seen |
| Best combo | `+0x2C70` (u32) | The highest combo payout |

**Each broken brick** (`func_800C7E5C`, given the brick's record): skipped if its flags (`+0x10`) have `0x20`. Otherwise, with n = combo steps + 1:

- an ordinary brick adds **10 × n** to the combo points, n at most 9;
- a brick flagged `0x40` adds **100 × n**, n at most 10.

**When the combo ends** (`func_800C7B4C`): its points are multiplied by **gravity count + fuse count + 1** (`+0x2AE6`, `+0x2AE8`: the "×" the original shows), become the best combo if higher, and are added to the score. The game's pop-up number shows the payout × 10.

**Checked in play** (Casual 1:1, random drops): a 3-brick combo paid 30 (300 shown) and a 5-brick combo 50 (500 shown): 10 a brick, steps 0, no gravity or fuse.

**Other points:** separate rises of 1,000 and 2,000 (10,000 and 20,000 shown) follow a combo; they match the known 1,000 for rubble turned to points (`func_8008E94C` adds to the score and the combo points). A stage's clear by `research_win` (every top cell one colour) scores through that path, in 1,000s and 5,000s, not through the combo.

**Still to find:** what flag `0x40` marks (a chain's later waves, or special bricks), so when the step multiplier rises; what `+0x12` marks and what the second total is for; the rubble and stage-clear points in full.

## What each magic does

`research_magic:K` (harness, `poke cmd:research_magic:K`) gives player 1 magic K and presses the magic button 0.8 s later; `research_board` logs the filled cells of each layer. Each magic used once, the cursor at the stage's start position, in Casual.

| K | Magic | Rescue 1:1 (2 layers) | Rescue 10:5 (7 layers) | Seen |
| --- | --- | --- | --- | --- |
| 0 | Firecracker | 108 cells, through to the core: the stage won | 34 + 70 cells from the top two layers | A target marker at the cursor, then a burst of fireworks |
| 1 | Dynamite | 146 cells: the stage won | 28 + 136 from the top two layers | The sticks fly in; a wide area of bricks flashes white and breaks |
| 2 | Magnet | 25 cells, a small hole at the cursor | Bricks from every layer (17 to 57 a layer over 4 uses) | The magnet stays and peels bricks off under the cursor, which the player moves (the owner, 2026-11-19), so it digs exactly where wanted |
| 3 | Atom | Every brick: the whole sphere cleared, the stage won | The top two layers gone entirely (1,024 cells each, over 3 uses) | The bricks change colour, then all break together: it wipes out whole layers rather than digging a hole |
| 4 | Bomb | 259 cells: the stage won | 22 + 32 + 69 + 130 from the top four layers | A target at the cursor, then a large blast that digs down several layers |
| 5 | Ray Gun | 15 cells | A few bricks from all seven layers, the bottom one included (8 to 59 a layer over 3 uses) | A narrow beam at the cursor that drills straight down to the core |

In short: the Firecracker and Dynamite clear wide patches of the top layers; the Bomb blasts down several layers; the Magnet digs wherever the player steers it; the Ray Gun drills a narrow hole to the core; the Atom wipes out whole layers.

**A press while a magic is still working is ignored.** Retried 2026-11-19 (the press repeated at four cursor places): the Magnet, Atom and Ray Gun all worked on 10:5; about one press in four did nothing and the magic stayed held, so the single press of the first run was such a miss.

**Still to find:** see the code section below.

## Each magic in the code (read 2026-11-20)

Use is dispatched in `func_800C719C` by the held magic: three routines serve the six. Every magic but the Atom breaks **whole bricks**: for each column in its pattern it takes the column's **top brick** (`func_800C61F0`) and breaks all that brick's cells, even past the pattern's edge, so the "top two layers" measured in play are the sphere's uneven surface, not deeper digging. Cells go through a queue of up to 260 (`func_800B4740` at `+0x168E`, run by `func_800B4A54`). Times are in game steps (60 a second).

| Magic | Routine | Area | Depth | Timing |
| --- | --- | --- | --- | --- |
| Firecracker | `func_800C6B74(2)` | A round disc of radius 4 at the cursor (49 columns; `func_800C6474`, radii {2, 4, 6} at `0x800E2AB0`) | Top brick per column | A 75-step countdown, the rocket, then the blast; it ripples outwards from the centre |
| Dynamite | `func_800C6BE0` | Five sticks, each a disc of radius 2 (13 columns), aimed about 4 cells ahead, 4 to each side slightly ahead, and 2 to each side behind | Top brick per column per stick | The sticks land one after another, about 46 to 105 steps after use |
| Bomb | `func_800C6B74(3)` | A disc of radius 6 (113 columns), then a second disc of radius 3 at the same centre on the layer it uncovered | About 2 bricks deep in the middle, 1 at the edge | As the Firecracker, the second blast a little later |
| Magnet | `func_800C5CFC(1)` | Every 5 steps, one random cell of the 3 × 3 under the **live cursor** loses its column's top brick | Keeps digging while held over a spot | Starts after 40 steps; lasts while its model's animation runs (length not yet known) |
| Ray Gun | `func_800C5CFC(3)` | Every 40 steps, all 9 columns of the 3 × 3 under the live cursor lose their top brick | One layer a step, down to the core | Starts after 120 steps; steerable like the Magnet; lasts while its animation runs |
| Atom | `func_800C51F0` | The top brick of every column on the board: a 17 × 17 area round the cursor first (some cells flashing, then breaking; others at once, by a 16 × 16 table at `0x800E9190`), then the other quarters | The whole surface, one brick deep | A 40-step countdown, then a sweep of 432 steps |

**In VS** (`0x8015D980` = 2, two-player) every magic acts as a Firecracker, and the rocket blasts a second time at the end of its animation.

**Points: a magic scores nothing itself.** No routine on the magic's path writes the score (`+0x2AF8`) or the combo (`+0x2AFC`); the per-brick scorer is reached only from the ordinary match routine. What scores is what follows: combos and gravity set off by the hole, and the stage's clear. Consistent with play: on 10:5 the Firecracker, Dynamite and Bomb were followed by small combos only (50, 130 and 90, shown × 10), and the large totals in 1:1 came with the stage won.

The Atom does not recolour the bricks into one combo, as it looked: it clears the cells directly (`func_8008CA34`).

**How long the Magnet and Ray Gun last (measured 2026-11-20, Casual 10:5, `BLOCKPLANET_SCORE_LOG=1`, which now logs the magic's flag `+0x1269` and the time):** the Magnet is active for **4.2 s** (about 250 steps: after its 40-step start, about 42 picks of a random cell of the 3 × 3, which took 73 cells from six layers where held still), the Ray Gun for **7.1 s** (about 425 steps: after its 120-step start, about 7 digs of the whole 3 × 3, which took it down through all seven layers to the bottom one). The test machine drew 14 frames a second; the game counts these in its real-time steps, as the drop timer, so the times should hold at any frame rate (to confirm on a faster machine).

**Where the Dynamite sticks land (measured 2026-11-20, `BLOCKPLANET_BLAST_LOG=1`, a hook on the blast routine `func_800C6474`, whose first two arguments are the blast's centre in 1/256 of a cell):** the five sticks land in a ring round the cursor, never on it, the same offsets each time (two uses, the cursor at 8,8 and at 5,10), in board cells:

| Stick | Offset from the cursor (x, y) | On screen (moving right lowers x, up raises y) |
| --- | --- | --- |
| 1 | 0, −3.1 | 3 below |
| 2 | −3.8, −1.8 | about 4 right, 2 below |
| 3 | +4.6, −2.2 | about 4.5 left, 2 below |
| 4 | −2.7, +2.7 | about 2.7 up and right |
| 5 | +3.3, +3.3 | about 3.3 up and left |

They match the code's flight steps (1/256 of a cell a step) carried on until each stick goes off. Each blast is a disc of radius 2 taking whole bricks, so the cursor's own cell is reached only by bricks that stick into a disc. The blasts' third argument rose with each stick (1,400 to 1,872 in the first use): perhaps a clock, not yet identified.

**The Atom's table and its order (2026-11-20).** The table at `0x800E9190` (dumped with the harness's `research_hex:800E9190:256`) is a **dome** of 16 × 16 values, 240 in the middle falling to 8 at the corners, symmetric, centred on the live cursor (`+0x2B20/+0x2B24`). `func_800C43E8` gives each cell near the cursor a time of t/2 + 281 from it (cells outside 1 to 15 of the cursor in either direction get 0) and compares it with the cell's ripple time (its distance from the Atom's centre × 6 plus a clock at `0x8014D260`). Measured with `research_snap` and `research_diff` every 0.25 s (Casual 10:5, the cursor at 8,8), the Atom empties the board's top bricks in this order:

1. at once, thin lines splitting the board into quarters through the cursor;
2. over about 1.5 s, the far side of the sphere, in waves moving outwards from those lines;
3. last, about 2 to 3 s in, the square in view round the cursor, **from its edge inwards: the cell under the cursor breaks last**, as the dome's values say.

So the table times the collapse of the part of the sphere the player sees (the middle last), rather than choosing which cells go.

## Power pieces and the combo's steps (read 2026-11-20)

**Flag 0x40 is a power piece.** Each board cell has an attribute byte (array at the pointer `0x800E44D4`, 9 layers of 1,024); its bits `0x60` hold 0x40 for a **power piece** and 0x20 for a **crystal piece** (Training's lessons describe both). `func_8008AF00` returns that byte & 0x60, and the match routine (`func_8009423C`) copies it into each broken brick's record. A broken brick also gets 0x40 when it fell by gravity, then matched, and touches a power piece (the check `func_800C8A8C` looks at listed neighbours and the cell below).

So **the combo's steps rise when power pieces break** (and gravity-fallen bricks beside them): each such brick adds one step and scores 100 × steps (at most 10), and every ordinary brick after it scores 10 × (steps + 1). A combo started on a power piece therefore pays far more per brick, and lasts longer.

**Where power pieces come from:** when a combo of 1 to 19 bricks ends (in every mode but Puzzle), after about a second its brick count is added to `+0x5A0`, and `func_800CD3B8` launches that many small objects (`func_800C18F8`) that each land on a brick and mark it a power piece (with a sound). Crystal pieces come the same way from `+0x5A2`. (A combo of 20 or more gives magic instead.)

## The second total: a mid-combo pop-up

`+0x2B04` gains the same points as the combo for most bricks (record `+0x12` set: ordinary matches; not a dropped piece that makes no fuse, nor the gravity-fallen bricks beside power pieces). It is only for show: `func_800B363C` adds to a floating 3D number (up to 4 at once, rising for 45 steps), it never touches the score. When a combo runs for 120 steps without a new counted brick (`+0x2B0A`), `func_800C7BE4(1)` floats the subtotal × 10 and clears it; at the combo's end the whole payout floats instead and the subtotal is dropped. So long combos (power combos) show part-totals as they go.

**The large grey number** (for example "6000" after a combo of about 59,000) is probably one of these: a mid-combo subtotal, or `func_800C7DB4`, which adds 1,000 + 1,000 × (gravity + fuse) to the score and floats it (gravity + fuse = 5 gives exactly 6,000); it is called from `func_800AA5A8` and six related functions (perhaps uncovering parts of the core). Not yet confirmed in play.

**Play checks still to do (2026-11-20).** A 30-drop random run in Casual 3:4 made no combo, so no power pieces appeared (`research_board` counted 0 throughout) and the power-piece rule above is not yet seen in play; a test needs a deliberate combo (for example by painting a few top cells one colour next to a power piece). The same run's queue log (`BLOCKPLANET_SCORE_LOG`) showed queue index 22, which the HUD treats as the "?" piece, in 7 of 21 queue states, more often than the wildcard is thought to come: whether 22 means "?" on every stage is to check before reading anything into it.
