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

**Still to find:** how long the Magnet and Ray Gun last (their animation's length); the Dynamite sticks' exact landing cells; the Atom's table (which cells flash and which go at once). The Atom does not recolour the bricks into one combo, as it looked: it clears the cells directly (`func_8008CA34`).
