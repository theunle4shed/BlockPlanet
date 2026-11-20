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

**The large grey number** (see the gravity lab below: most likely the core bonus) (for example "6000" after a combo of about 59,000) is probably one of these: a mid-combo subtotal, or `func_800C7DB4`, which adds 1,000 + 1,000 × (gravity + fuse) to the score and floats it (gravity + fuse = 5 gives exactly 6,000); it is called from `func_800AA5A8` and six related functions (perhaps uncovering parts of the core). Not yet confirmed in play.

**Play checks still to do (2026-11-20).** A 30-drop random run in Casual 3:4 made no combo, so no power pieces appeared (`research_board` counted 0 throughout) and the power-piece rule above is not yet seen in play; a test needs a deliberate combo (for example by painting a few top cells one colour next to a power piece). The same run's queue log (`BLOCKPLANET_SCORE_LOG`) showed queue index 22, which the HUD treats as the "?" piece, in 7 of 21 queue states, more often than the wildcard is thought to come: whether 22 means "?" on every stage is to check before reading anything into it.

## Sliding and extending a combo (read 2026-11-20; the owner's question: can a white, breaking brick be moved to set off another combo?)

- **Slide:** hold B (pad 0x4000; `func_800ACB94` sets `+0x2B52`), and `func_800954FC` moves the grabbed piece each frame. A piece can be grabbed on layers 1 to 7 when it is whole and of an ordinary kind (`func_8009532C`: the piece id between two bounds, probably leaving out rubble and walls); it moves onto free cells on its layer, sinks over a hole and can step up a layer; the cell it left is checked for gravity. Puzzle counts slides down (`+0x25C8`).
- **Breaking bricks are locked:** a brick in a running combo carries 0x400 in its record's state (`func_80093C08` with lock 1), and a grab on such a brick is refused (`0x80095928`). So an ordinary white, flashing brick cannot be moved.
- **The one unlocked case:** the brick that fell and set off a gravity combo (state 0x16) has no lock. It can be grabbed (its break is cancelled), slid next to matching bricks and let go; on letting go the match check runs at its new place (`func_800C84C8`), and if it matches, the game counts a **fuse** (`+0x2AE8` + 1 at `0x800CE9A8`) and adds 3 to the magic meter.
- **A conflict to settle in play:** the earlier note (blockplanet-game-state.md, 2026-11-15) took `0x800CE9A8` for a piece *dropped* into a running combo; this reading puts it on the slide's release (`+0x2B60` set), with no other code adding to the fuse count. The owner remembers dropping a piece onto a flashing combo. Test: set off a gravity combo, hold B on the brick that fell, slide it beside matching bricks, let go, and watch `+0x2AE8` (`BLOCKPLANET_SCORE_LOG`); and separately drop a piece onto flashing bricks and watch the same.

## The combo lab (2026-11-20, the owner's idea: set each mechanic up to test it, and later to teach it)

`poke cmd:research_lab:combo` (harness; Casual 1:1, in play) rebuilds the top layer so one drop makes a combo. Every layer-1 cell becomes an inert wall (value 423); the **piece about to be dropped** (the queue's first entry, `+0x2374`, as the cursor's shadow shows; `+0x2386` is the piece taken last) is cut out of the layer at the cursor down to the core; and two pieces of the same shape are written beside the hole at two of the shape's fitting positions (its record's list at `+0x74A`/`+0x74C`), each cell with its value (record value + cell number − 1), its texture table (record `+0x78` + 96 × (cell − 1)), the column byte 0x11 and face masks (top 16, plus 8/4/2/1 for an empty neighbour at x+1/x−1/y+1/y−1). A piece's layout cell (x, y) sits at the cursor + (x − 1, y − 1) (checked: a lying bar landed at cursor x − 1 to x + 1).

**First result:** with the lying bar in hand (fitting positions ±3 across and ±1 down), the two bars were set 3 cells to each side; one drop at the cursor broke all three pieces in one combo and scored 30 (300 shown). So the per-brick scoring counts **pieces**, not cells: 10 per piece. `research_lab:combo:I:J:SX:SY` picks other fitting entries and signs. Next labs: gravity, fuse by sliding, fuse by dropping, power piece.

**The game's own fuse lesson** (Training, Advanced; summarised in [blockplanet-training-review.md](blockplanet-training-review.md)) matches the slide reading: a breaking piece flashes and is a "fuse"; a fuse from a power combo can be slid into line with two or more pieces to set off a new combo. It says nothing of dropping a piece onto a running combo.

## The gravity lab (2026-11-20)

`poke cmd:research_lab:gravity` (harness; Casual 1:1) builds the combo lab's first combo with the piece about to drop (shape K: the hole at the cursor and pieces A and B), plus a piece of the stage's other shape (L), F, on layer 2 resting on A (the lab tries origins round A for one that covers part of A and nothing else; its other cells are cut out of layer 1 beneath it), and two L pieces on layer 1 at L's fitting positions round F's landing place. The second shape matters: anything of K's shape next to A's place would fit A and join the first combo.

**Result (checked in play):** with the square in hand, the drop broke the three squares; the bar F lost its support, fell to layer 1 and matched the two waiting bars. The gravity count (`+0x2AE6`) rose to 1, and the combo paid 120: its 6 pieces × 10, **doubled by the X count (gravity 1 + fuse 0 + 1)**, the first check in play of the multiplier. The score also gained 2,000 as the fall opened core cells: `func_800C7DB4`'s 1,000 + 1,000 × (gravity + fuse) with gravity 1, which makes it the likely source of the large grey number ("6000" = 1,000 × (1 + 5)).

## The fuse lab (2026-11-20, first runs)

`poke cmd:research_lab:fuse` builds the combo lab's first combo with A (the piece on the −x side) a **power piece** (attribute 0x40 on its cells' face mask bytes) and, beside B (+x), an empty corridor running up (+y) to a place where two more pieces of B's shape (E, G) fit B but not B's start. `research_fuse_auto:A|B` makes the game itself, as soon as a combo starts (`+0x2AFC` above 0), put the cursor on that piece, hold B, press up once per corridor cell and let go (a script cannot hit that moment).

**Power pieces, checked in play:** the combo's bricks scored 100 (the power piece, steps 0 → 1), then 20 (an ordinary piece, 10 × 2), then 200 (a flagged piece, 100 × 2): 320 for three pieces, against 30 in the plain combo lab. So power pieces make the combo's steps rise and pay about ten times as much, as read from the code.

**Sliding:** a grab at the combo's very start, before B had joined it, slid B up the corridor as an ordinary resting piece (an extra press took it on top of E, a layer up); a grab once B had joined the combo did nothing, and B broke with it. So bricks already in this combo are locked, as the code says. Still to find: the "power combo" whose flashing pieces the original's fuse lesson says can be slid. This combo had a power piece in it but ended in about 0.3 s, while the lessons describe a power combo as one started with a power piece that takes longer; next, a combo started by dropping onto a power piece (or with a power piece in hand, made from crystals), then the slide.

**The fuse combo, checked in play (2026-11-20).** With B itself a power piece (`research_lab:fuseB`, and the same with the hole flagged too, `fuseBH`) and the game grabbing B 120 ms after the combo started (`research_fuse_auto:B:120`): the first combo paid 400; B, flashing, was grabbed and slid up the corridor; on letting go the **fuse count rose to 1** and B broke the two waiting pieces in a second combo, which paid 20 × 2 = 40 (doubled by the X count), and the cells it opened gave the core bonus of 2,000 (1,000 × (1 + fuse 1)). With only the hole flagged (`fuseH`), B stayed locked and broke with the first combo.

So **a power piece breaking in a combo can be grabbed and slid; ordinary breaking pieces are locked**. Slid into line with matching pieces and let go, it sets off a fuse combo, counted when it is let go, as the code says (`0x800CE9A8`) and as the original's fuse lesson teaches ("a fuse from a power combo"). This is the owner's memory of moving a white block to chain a combo. Dropping a piece onto a running combo is not counted as a fuse in the code (not tested directly).

## The magic lab (2026-11-20)

Each magic used once at the cursor on Casual 10:5 (seven layers), the board snapshotted before (`research_snap`) and compared 10 s after (`research_diff`), the magic given by `research_magic:K` and pressed again with `research_magic_retry` only if still held (each was used exactly once). Cells emptied, by layer from the top (6) down:

| Magic | Cells | By layer | Shape round the cursor |
| --- | --- | --- | --- |
| Firecracker | 69 | 6: 69 | One round patch about 4 cells across each way, centred on the cursor, top layer only |
| Dynamite | 94 | 6: 94 | Five small patches in a ring round the cursor, the cursor's own cell left |
| Magnet | 81 | 6: 18, 5: 15, 4: 16, 3: 18, 2: 6, 1: 8 | The 3 × 3 under the cursor (with whole bricks round it), dug down to the bottom layer when held still |
| Atom | 1,024 | 6: 1,024 | The whole top layer |
| Bomb | 168 | 6: 131, 5: 37 | A larger round patch about 6 cells each way, and a second layer in its middle |
| Ray Gun | 86 | 6: 18, 5: 20, 4: 14, 3: 14, 2: 10, 1: 10 | The 3 × 3 under the cursor, drilled straight down to the bottom layer |

All match the code's reading (Firecracker radius 4, Bomb radius 6 then 3, Dynamite five discs of radius 2 in a ring, Magnet and Ray Gun the 3 × 3 under the cursor, Atom the whole surface; whole bricks reaching past each pattern's edge). No magic scored by itself (the Ray Gun's run gained 1,000 from a core cell it uncovered). The heat maps are drawn from the logs by a script kept outside the repository; a magic lesson would show these shapes.

## A striped board for the magic lessons (2026-11-20, the owner's idea)

The owner's idea: a sphere whose layers alternate in colour, so a magic's damage shows at once how many layers down it went. Tried in the harness:

1. `research_lab:stripes`: every cell a single cube of one colour per layer. **The game cleaned the board up at once** (single cells are not whole pieces) and counted the stage as won (score 261,000). Boards must be built from whole pieces.
2. `research_lab:stripes2:N` on Casual 1:1 (its shapes: the 2 × 2 square, blue, and the lying bar, yellow), N layers (6; more than the stage's own one layer, accepted and drawn): layers alternating 2 × 2 squares (the one shape that tiles the 32 × 32 board) with **inert walls**: the board held, and looked clean, but **magic does not break walls**: the Ray Gun took 16 cells of the top layer and stopped at the wall layer beneath.
3. Now: squares alternating with **lying bars** (ten bars a row, with a two-column strip of squares round the back of the sphere, opposite the cursor, to make up the 32 columns).

**Result (2026-11-20):** squares alternating with lying bars works. Six layers on Casual 1:1, each magic once at the cursor: the **Ray Gun** drilled a tunnel through all six layers to the core (24, 24, 20, 21, 20, 14 cells, layer 6 to 1, and 12 core cells), its stripes plain to see down the sides; the **Bomb** took the top layer (140 cells, the yellow layer beneath showing) and a second layer in its middle (51, blue again); the **Firecracker** took the top layer only (68). The rings of colour make each magic's depth readable at a glance: the board for the magic lessons.
