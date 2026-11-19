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
| 2 | Magnet | 25 cells, a small hole at the cursor | Not used (see below) | The magnet pulls bricks out at the cursor |
| 3 | Atom | Every brick: the whole sphere cleared, the stage won | Not used | The bricks change colour, then all break together |
| 4 | Bomb | 259 cells: the stage won | 22 + 32 + 69 + 130 from the top four layers | A target at the cursor, then a large blast that digs down several layers |
| 5 | Ray Gun | 15 cells | Not used | A beam on a small patch at the cursor |

So far the Bomb is the only one seen digging deeper than two layers; the Firecracker and Dynamite clear wide patches of the top layers; the Atom cleared a whole 1:1 sphere.

**Not used on 10:5:** the Magnet, Atom and Ray Gun stayed held (`+0x702` kept its value) and the piece in hand was dropped by the drop timer instead; in 1:1 all three worked. Whether they need a condition (a brick of some kind under the cursor, the piece in hand, a lower layer count) or the harness's press missed is still to find.

**Still to find:** each magic's area in cells by design (from the code), the points each scores, why three were not used on 10:5, and the Atom on a deep stage.
