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

**Still to find:** each magic's area and depth by design (from the code), how long the Magnet lasts, whether the Atom always takes the top layers or one kind of brick, and the points each scores.
