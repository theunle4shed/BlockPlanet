# How a stage's sphere is built

Research session, 2026-11-18, on the owner's question: is there one seed for the whole sphere, one per layer, or is each block chosen at random as it is placed?

Everything here was found in the player's own US rev 0 ROM, with the Linux build and the research switches below. As AGENTS.md requires, no tables, dumps or disassembly from the game are in this note: it describes behaviour in our own words, gives addresses for finding things again, and says what each claim rests on. It is written so that it can be shared with the community.

## The short answer

- **One sphere, one seed (two numbers, in fact).** At the start of a stage the game draws a 16-bit **sphere seed** and restarts its random-number generator from it just before building. It also uses player 1's **piece seed** (a second 16-bit number drawn just before it, which also starts the "Next" piece queue). With both numbers forced, the same stage is built identically every time (confirmed, below). So there are 65,536 × 65,536 possible spheres for each stage's settings.
- **Not one seed per layer.** The layers are built one after another, bottom up, from the same continuous stream of random numbers. A layer's contents depend on the seed and on everything drawn before it, so the seed cannot change one layer alone.
- **Not chosen block by block.** Blocks are not picked cell by cell: whole pieces (the same shapes the player drops) are dropped at random spots, stacking up to the layer being built, and the gaps left are filled with single blocks.
- **Each mode has its own settings, but most share one builder.** Rescue, Lines, Hide & Seek, Time Trial, VS CPU and the attract demo all use the same builder, with different settings records. Training and Puzzle load stored layouts instead.

## The random-number generator

| | |
| --- | --- |
| Routine | `func_80081E90`. Takes a range *n* and returns a number from 0 to *n* − 1. |
| State | One 32-bit word at `0x800E2AA0`. |
| Starting value | A fixed number in the game's data, the same at every power-on. Nothing ever seeds it from a clock or a timer: it only moves when something draws from it. |
| Step | A hand-written 32-bit generator built from 16-bit halves. It multiplies the low half, mixes shifted copies of both halves into the high half, and adds 1. It is not a textbook multiply-and-add generator (it matches none of the common multipliers), and no repeat was found in 2 million steps from a test seed. |
| Output | The new state scaled to the range: roughly *state* × *n* / 2³², so the high bits decide. |

Confirmed: a re-implementation of the step reproduced every one of the 20,505 sphere-stream draws logged during a five-layer build. (The re-implementation is kept out of the repository for now; the owner decides whether it is published.)

**Why every start is different.** The state is never seeded, but it is drawn from all the time: the menus, the attract demo and the stage starts all take numbers. How many draws happen before a stage starts depends on timing, so the sphere seed drawn at the start is effectively random (seven scripted starts, with the same key presses, drew seven different seeds).

**The piece stream.** Each player also has a piece seed in the player context (player 1: `0x801125BC`, copied to `0x80115824`). To draw a piece, `func_8008A6E4` saves the main state, runs the generator once from the piece seed, updates the piece seed to *seed* XOR *next state*, and puts the main state back. So piece draws never disturb the sphere stream. Confirmed on all 10,270 piece draws of a five-layer build.

## Building a stage, step by step

1. **New stage (`func_800CC6E0`).** It draws the piece seed (range 65,536) and then the sphere seed (range 65,536). Then, **for each player**, it puts the sphere seed back into the generator state before building that player's sphere. So in VS CPU and two-player games both spheres are identical (seen in VS CPU: the two builds gave the same layers). It is the builder's only caller, so every stage start goes through it.
2. **Settings (`func_800906E8`, the builder).** It reads the mode (`0x800E44A8`) and picks one of nine set-up routines (table below). Most of them load one 20-byte **settings record** from a single table in the ROM into `0x8013D270`, and `func_80088914` unpacks it.
3. **The piece queue.** 25 piece draws (range 8: one of 8 slots, see below) fill the queue.
4. **Special cells.** 8 positions are drawn (16 draws, range 32 each). Two special cells (value 63) end up in each layer except the top one.
5. **The layers, bottom up.** For each playfield layer, about 2,048 attempts: take the next piece from the queue, draw a column and a row (0 to 31 each), and drop the piece there (`func_8008B830` and its helpers; the ROM's debug messages name this part of the code after a level generator and piece dropping). A piece that would not fit flat on the layer, or would break another piece, is not placed. The number of attempts does not depend on the seed: chapter 1's one layer always took 6,173 draws, chapter 10's five layers 30,777.
6. **Gap filling.** When a layer's attempts are used up, about three quarters of it is covered by pieces; every gap left is filled with a single block (value 9), so the layer is full.

The fill order was confirmed by logging how full each layer was every 600 draws: layer 1 rose to about 760 of 1,024 cells and jumped to 1,024, then layer 2 started, and so on.

## The settings "matrix"

There is a matrix of sorts: one table of settings records, one record for each stage of each mode, which the game loads when the stage starts. The records are packed in a ROM file, so they were read as the game loaded them (`BLOCKPLANET_SPHERE_RESEARCH=1` logs each one), not extracted.

**Which record each mode uses** (set-up routines in `func_800906E8`'s jump table; record numbers count from 0):

| Mode (`0x800E44A8`) | Set-up routine | Record |
| --- | --- | --- |
| 1 Rescue, 10 attract demo | `func_800898D4` | level + 10 × chapter (`0x800E44A0`) |
| 9 Lines | `func_80089AE8` | 100 + level + 10 × chapter |
| 6 VS CPU | `func_80089B94` | 150 + level |
| 2 Time Trial | `func_80089C0C` | 157 + level |
| 7 and 11 (not identified) | `func_80089494` | 177 + level |
| 12 (not identified) | `func_800896F0` | from the chapter, like Rescue |
| 5 Hide & Seek | `func_800890B8` | several records; 100 extra draws (range 3) and picture cores |
| 3 Training | `func_80088E50` | no record: a stored, scripted layout (17 draws) |
| 4 Puzzle | `func_80088C90` | no record: a stored puzzle layout (17 draws) |

**What the record's bytes do** (from `func_80088914` and from records seen in play):

| Byte | Meaning | Status |
| --- | --- | --- |
| 0 | Number of playfield layers (Rescue uses 1 to 6). | Confirmed |
| 2 | Number of piece kinds in play (copied to `0x800E44B8`; the piece draws use range 4 × this). Rescue uses 2 to 4. | Confirmed |
| 9 | Half the side of the core square to uncover to win: doubled into the player's `+0x23E4` (kept within 2 to 10), the core opening ([level data](blockplanet-level-data.md)); Rescue 1-1 has 2, the 4 × 4 square its start card names. In Rescue it grows with the level. | Confirmed with the earlier notes |
| 10 | Picks a value 1 to 11 from a list in the game's data. | Unknown |
| 11 | A bit mask over the six piece kinds, one bit each, in the order of the game's list of six records (`0x800E4508`): bit 0 the square (33), 1 the flat bar (32), 2 the standing bar (34), 3 the L (35), 4 the S (36), 5 the T (37). In every Rescue record it has as many bits set as byte 2. | Confirmed (matches the kinds seen in play at Rescue 1-1, 3-2, 10-1 and 10-8) |
| 3 to 8, 12 | Further settings; byte 6 holds three on/off flags, byte 12 a count of entries that follow. Hide & Seek records use bytes 7 and 8. | Unknown |

**Rescue's stages, all 100 (surveyed 2026-11-18).** `BLOCKPLANET_RECORD_SURVEY` loaded each record with the game's own loader (records 0 and 90 matched what chapter 1 and chapter 10 load in play):

- **Piece kinds:** 2 in 37 stages, 3 in 61, 4 in 2. Episodes 1 and 2 always use 2. Kinds become 3 from Episode 3 (stages 5 and 8 first), and are always 3 in Episodes 5 to 9. Episode 10 starts with 2 and reaches **4, the most in Rescue, in its stages 8 and 10**. Stage 10-8 was started to check: its layers hold four kinds (33, 34, 35 and 37), drawn in four colours, and its piece draws use range 16.
- **Layers:** 1 (only the first stage), 2 in 9 stages, 3 in 15, 4 in 41, 5 in 23 and 6 in 11 (Episode 7's stages 8 and 10, and Episode 10 from stage 2).

- **How often each kind appears** (from byte 11): the flat bar in 52 stages, the standing bar in 45, the square in 44, the S in 43, the T in 42 and the L in 39 (first in Episode 1, stage 7).

The list per stage stays in the private research logs, as AGENTS.md requires.

**How this links to the Next pieces.** The builder and the Next queue in play use the same piece generator: `func_8008A6E4` draws a slot from player 1's piece seed, `func_8008A63C` turns the slot into a kind using the stage's number of kinds (`0x800E44B8`, from byte 2), and `func_8008A2BC` pushes it onto the queue. Besides the builder, only `func_800CD3B8` calls it, which is the in-play path (inferred from the call list). So one setting controls both: editing byte 2 (and byte 11) changes the sphere's kinds and the pieces handed out in play together, as the one-kind and six-kind tests showed. And the piece seed carries on from the build into play, so the two seeds also fix the order of the pieces you are given (inferred, not yet compared piece by piece).

**How many of one kind? There is a balancing rule.** A piece draw (range 8) does not pick the kind itself: it picks one of 8 slots (`func_8008A63C`), and each slot hands out the kinds in turn, then moves on to the next (the slots start staggered, `func_8008A5F0`). So the pieces drawn stay almost exactly half and half whatever the seed. Simulated for all 65,536 piece seeds over one layer's 2,075 draws: one kind is never more than 4 pieces ahead of the other, and never more than 8 come in a row (the longest run is 5 to 8, depending on the seed). The pieces *placed* are less even (one layer had 140 of one kind and 91 of the other), because a larger shape fails to fit more often. Pieces covered 66 % to 81 % of each of 75 measured layers; single filler blocks (and the special cells) the rest.

**Can a seed give a sphere of one kind?** No. With two kinds in the settings, the slot rule hands out both, whatever the seeds. A one-kind sphere needs the settings changed: byte 2 of the record is the number of kinds (read into `0x800E44B8`), so a record with 1 there, or a hand-made layout, does it. **Tested (2026-11-18):** chapter 10 with both seeds forced and `BLOCKPLANET_STAGE_RECORD_EDIT=2=1` built five layers of one kind only (the first kind of the stage's pair, the green one), the piece draws used range 4 instead of 8, and the Next queue in play showed only that kind. With one shape only, more pieces fit: about 270 a layer instead of about 140 + 100, and about 200 single filler blocks a layer instead of about 270. The stage started and played normally for the 25 seconds watched.

**All six kinds at once (tested 2026-11-18):** the same start with `BLOCKPLANET_STAGE_RECORD_EDIT=2=6,11=0x3F` built every layer from all six kinds (piece draws in range 24), drawn in six colours, and the Next queue in play mixed them. Kinds 32, 34 and 35 are 3-cell shapes and 33, 36 and 37 are 4-cell shapes; each layer held about 21 to 53 pieces of each kind (fewer of the 4-cell ones, which fit less often) and about 260 to 290 single filler blocks. It started and played normally for the 25 seconds watched. No piece seed degenerates either: all 65,536 keep using all 8 slots, with no short repeating cycle.

## More layers, and what happens to drops that make no combo

Tested 2026-11-18 with `BLOCKPLANET_PLAY_LOG=1`, which logs player 1's hearts and each layer's fill whenever they change (`BLOCKPLANET_KEEP_HEARTS=1` keeps the hearts at 3 so that dropping can go on), on Episode 10 stage 1 with both seeds forced, pressing drop every few seconds.

- **The most layers is 7.** Byte 0 = 7 builds seven full playfield layers, and the stage starts with a larger sphere. Byte 0 = 8 or 9 spends the extra layers' draws but leaves grid layer 8 empty, so the grid holds at most 7 (layer 8 is the board's one layer of headroom, and there is no layer 9).
- **A drop that makes no combo stays on top and costs a heart.** The piece lands one layer above what is under it and stays there, sticking out of the sphere; about 1 to 3 seconds later a heart is lost. Dropped again on the same spot, pieces stack higher (layers 2 to 6 seen on a one-layer stage). Three such drops on a new stage end the game. A drop that makes a combo (for example on a matching piece in the stack) clears and costs nothing; a drop that cannot land at all also costs a heart.
- **The ceiling does not move.** Pieces sit in the same grid as the sphere, up to a fixed top, and no piece ever landed in layer 8. So the sphere's own layers use up the room: a normal stage (at most 6 layers) leaves room above for stacking, while on a 7-layer sphere no drop could land anywhere, and every drop cost a heart. A 7-layer sphere is therefore not playable by drops (sliding and magic were not tried). That is probably why the game itself stops at 6.
- **Seen in passing:** after many lost hearts on the 7-layer sphere, the Next queue offered a "?" wildcard piece.

**Raising the ceiling.** The board is 9 layers (0 to 8) by design, and its size is built into the code: the four tables (block values, face masks, entries, column map) are fixed-size, and about 20 functions compare layer numbers with 8 or 9 directly (about 60 places: the builder, the board helpers, the column-map builder, the landing and combo code, the layer-8 drawing pass) besides about 70 that go through the board helpers. What it would take, smallest first:

1. **Let pieces rest in layer 8.** Investigated 2026-11-18 (reading the code, and temporary log-only hooks that were then removed); nothing is changed yet.
   - **Why pieces never stay in layer 8:** a drop is not refused in advance. The in-play drop (`func_800CD3B8`) asks `func_8008AB0C` for the landing layer (the top of the tallest column under the piece's 3 × 3 footprint, plus one, through `func_8008A9C4`) and calls the place routine (`func_8009423C`, the ROM's "place piece in game") with it, whatever it is: on a 7-layer sphere it was 8 on every drop. But the routines that write a block into the board accept layers 0 to 7 only and silently do nothing for 8 or more. There are seven of them: `func_8008B220`, `func_8008B424`, `func_8008B62C`, `func_8008BE58`, `func_8008C01C`, `func_8008C1E4` (block values and entries) and `func_8008CA34`; the place routine reaches them through `func_8008E244`. So the piece is "placed" with no effect, vanishes, and the drop counts as one without a combo: a heart is lost.
   - **Layer 8 is otherwise ready:** the tables have room for 9 layers (0 to 8), the face-mask helpers (`func_8008C95C`, `func_8008C9CC`) accept layer 8, the column-map builder (`func_8008B0E0`) searches layer 8 for a column's top, and layer 8 has its own drawing pass (`func_8009FA20`). In none of the logged frames did anything else use layer 8.
   - **The aiming X:** the shadow code (`func_8009AB60`, at `0x8009B19C`) shows the X when the landing layer is 8 or more.
   - **What it would take:** let the seven write helpers accept layer 8, and move the shadow's X to a landing layer of 9. Hooks cannot change the constant inside an instruction, but a hook at each helper's start can do the layer-8 write itself and return, and one at the shadow check can adjust the value it compares. About nine small hooks. Risks to test: combos and gravity reaching layer 8, the layer-8 drawing pass with real pieces in it (the cell setup's "capped at 7" flag), magic, and whether anything else expects layer 8 to stay empty. The ceiling would then move from layer 7 to layer 8: one more layer of room, as option 1 promises, not more.
2. **A taller board.** Move the four tables to the free upper 4 MB (their pointers are set per player in `func_800AF294`, so they can point anywhere; about 7 KB per extra layer), change every layer bound (hooks cannot change a constant inside an instruction, so this means rewriting those spots in the generated code, as `tools/recomp/generate.py` already does for other patches, or replacing the board helpers with BlockPlanet's own), extend the mesh builder's layer steps, and pull the camera back (higher layers come closer to it). The column map's 4-bit layer numbers allow up to 15 layers. Large.

These describe drops in Rescue with the heart rule as seen; the code that decides the heart loss (`drop.c` in the ROM's messages, around `func_8009423C`) is not read yet.

## Piece kinds: how they are defined, and what a new one would take

Investigated 2026-11-18 by reading the code only; nothing is changed.

**How a kind is defined.** Each kind is a shape record (fields in the [game-state notes](blockplanet-game-state.md): block value, material, cell count, footprint size, shape ID, then layout data), and the level's shape table at `0x8014CBC8` points at the stage's records (entries 0 to n − 1, with fixed ones such as the "?" piece from entry 22). The records are not loaded as data: `func_800857E8` (about 3,100 instructions) writes every one in code, each kind in turn with its block value (288, 297, 306, 315, 324, 333) and material (32 to 37), its 3 × 3 cell layout, and per-cell data written by 192 calls to `func_800826D0` (not decoded; probably which sides of each cell join the next cell, for drawing the piece as one brick).

**The shape record, decoded (2026-11-18).** From memory dumps in play (Episode 6, stage 1) matched against the builder's code. Each record is `0x768` bytes; the six piece kinds' records sit one after another from `0x80127CF0` (pointers at `0x800E4508`), and the stage's shape table points at the ones it uses.

| Offset | Size | Meaning | Status |
| --- | --- | --- | --- |
| `+0x0` | s16 | Block value of cell 0 (9 × material) | Confirmed |
| `+0x2` | s16 | Material (the colour), 32 to 37 | Confirmed |
| `+0x4` | s16 | Number of cells | Confirmed |
| `+0x6`, `+0x7` | u8 × 2 | Size of the layout box the builder uses (2 × 2 or 3 × 3) | Confirmed |
| `+0x8` | u8 | Shape ID, 1 to 6 | Confirmed |
| `+0x9` | u8 | 128 for the square, 0 for the other five kinds; probably a flag (perhaps for a piece that looks the same from every side) | Unknown |
| `+0xA` | s16 × 9 | The 3 × 3 layout: entry *x* × 3 + *y* holds the cell's number (1 to *n*), 0 where the piece has no cell. A cell's block value is the record's value plus its number minus one | Confirmed (layouts match the shapes on screen) |
| `+0x1C` | s16 × 25 | A 5 × 5 map, one cell larger than the layout on every side, marking the cells around the piece (sides and corners) | Confirmed as data; its use (probably finding the bricks a piece touches) is inferred |
| `+0x78` | 9 × 96 bytes | For each cell, 6 faces × 4 corners of texture coordinates: which tile of the brick texture each face shows, and turned which way, so a piece's cells join into one brick with edges only on its outside (`func_800826D0`, 192 calls in the builder) | Confirmed in the code |
| `+0x738` | u8 × 9 | One byte per layout cell (*x* × 3 + *y*), 0 where the piece has no cell: 16 plus the cell's open (unjoined) sides, N 1, S 2, W 4, E 8. One exception: the T's centre (open only to the S) has 19, not 18. Read by `func_800A3894`, `func_800AA20C`, `func_800B3668`, `func_800BFDA4` (use not read) | Confirmed as data |
| `+0x742` to `+0x748` | s16 × 4 | The piece's own box inside the layout: width, height, then the column and row it starts at (the flat bar: 3, 1, 0, 1). Read by `func_800B3DEC`, `func_800BC850`, `func_800BCB50`, `func_800C8E24` | Confirmed as data |
| `+0x74A` | s16 | Number of entries in the list that follows: 4 to 12 | Confirmed as data |
| `+0x74C` | byte pairs | The positions (column, row, stored with the column mirrored) at which a second piece of the same shape counts as fitting against this one, **which is what decides a combo**. For the L, S and T it is every position where the second piece touches without overlapping; for the square and the two bars only the lined-up ones (end to end, side by side). Up to 14 fit in the record. Read by the combo code (`func_800C8210` to `func_800C9184`) and `func_800B0B30` | Confirmed (rule matches all six kinds; the combo role was shown with the cross, below) |

**The six kinds:**

| Material | Shape ID | Cells | Shape |
| --- | --- | --- | --- |
| 32 | 1 | 3 | Bar, lying across |
| 33 | 2 | 4 | 2 × 2 square |
| 34 | 3 | 3 | Bar, standing up |
| 35 | 4 | 3 | Small L (3 cells) |
| 36 | 5 | 4 | S |
| 37 | 6 | 4 | T |

This corrects the game-state notes, which guessed that IDs 3 and 4 were L shapes: 3 is the standing bar.

**Rotation: there is none.** The two bars are separate kinds with their own colours, so a piece's orientation is part of its kind. Nothing in the game rewrites a layout after the builder: the six records did not change across memory dumps while pieces were dropped and every other button was pressed, and none of the routines that read a layout (landing, overlap, placement) take a rotation. A rotating piece in BlockPlanet would therefore be new game logic: the simplest form would swap the piece in hand for another kind's record (for example the two bars), which keeps every check correct.

**What a new record needs** (for option 1 below): the header (value, material, cell count, box size, shape ID), the 3 × 3 layout, the 5 × 5 surround map, the box at `+0x742`, the list of fitting offsets, and the texture coordinates for every face of every cell. All but the last can be worked out from the layout; the texture coordinates follow the builder's pattern (edge tiles on outside faces, joining tiles between cells).

**Face textures: the rule for a brick (worked out 2026-11-18).** Each face's entry comes down to two numbers: a tile of the brick texture (a strip of 16 tiles; the second set at `+0x3D8` holds the same choice for a half-size copy of the texture) and a number of quarter turns. Faces 0 to 3 are the cell's sides (called N, S, E and W here, after the layout's rows and columns), faces 4 and 5 its top and bottom, which are always the same. The rule below, applied to each kind's layout, reproduces every face of all six kinds exactly (126 of 126, both sets, byte for byte, checked against memory dumps):

| Face | Situation | Tile, turns |
| --- | --- | --- |
| Side | Joins another cell of the piece | 14, 0 |
| Side | Outside, no cell of the piece on either side along it | 5, 0 (an end cap) |
| Side | Outside, a cell of the piece on one side along it | 4, 2 if that cell is to the W (for N and S faces) or N (for E and W faces); 4, 0 if E or S |
| Side | Outside, cells on both sides along it | 3, 0 (the middle of a long side) |
| Top, bottom | Joined on one side (an end cell) | 4; turns 0 if joined N, 1 W, 2 S, 3 E |
| Top, bottom | Joined on two opposite sides | 3; turns 0 for N and S, 3 for E and W |
| Top, bottom | Joined on two sides at a corner, the diagonal cell filled (as in the square) | 2; turns 0 NW, 1 SW, 2 SE, 3 NE |
| Top, bottom | Corner, diagonal empty (as in the L and S) | 7; turns 0 SW, 1 SE, 3 NW (NE, 2, inferred) |
| Top, bottom | Joined on three sides | 6; turns 2 when the open side is S (the T). Other directions inferred: 0 open N, 1 W, 3 E |

**Not covered by any piece in the game:** a single-cell piece and a cell joined on all four sides (a cross); tiles 0, 1, 8 and 9 are never used by pieces and are the likely candidates. Single blocks are different: the filler, the special cell and Hide & Seek's single blocks (materials 1 to 7) each use one whole tile (10 to 15) on every face with a fixed turn, and the core and picture tiles (materials 8 to 25) use another mapping.

**Material 38 is the "?" piece** (value 342, shape ID 7, tile 10 on every face), not a free material, so the seven materials the game accepts (32 to 38) are all used, and a new colour would need those range checks widened (or a piece to borrow an existing material's colour).

**Building a whole record from a layout.** Every field above can be generated from the 3 × 3 layout alone. Generated that way, the six kinds' records match the game's byte for byte (all 1,896 bytes, apart from the square's flag at `+0x9` and the T centre's byte at `+0x738`).

**Experiment: a five-cell cross (2026-11-18, temporary code, not committed).** Every material the game accepts is taken, and a cell's block value and drawing come from its material, so the cross replaced the T's record (material 37, red) at the start of the stage, built from its layout as above. On Rescue 1-1 with the kinds set to the square and the cross (`BLOCKPLANET_STAGE_RECORD_EDIT=11=0x21`):
- **It builds and draws.** The builder placed 60 crosses in the layer (values 333 to 337, five cells each); fewer fit than other shapes, so more single filler blocks were left. They were drawn as joined red crosses; the stage played normally.
- **The centre cell (joined on all four sides)** has no tile in the game's rule. Tiles 0 and 1 give a clean centre; tiles 8 and 9 show a mark, so they are meant for something else.
- **The fitting list decides combos.** With every piece a cross (`2=1,11=0x20`) and 16 drops at moving spots: with only the four tip-to-tip positions listed (like the bars, a design the owner suggested), no drop made a combo (16 hearts lost); with all 12 touching positions, 5 drops made combos, and the stage was won and the next one built. So "ends only" works as a rule but makes a random sphere very hard, because the builder seldom leaves crosses tip to tip.
- **Still to do for a real new piece:** its own colour (all seven materials are taken), its byte at `+0x738` for the centre (16 was used), BlockPlanet's Next picture (it showed the T, since the shape ID was kept), and a choice of combo rule.

**How pieces avoid overlapping.** Every piece fits a fixed 3 × 3 layout (nine halfwords from the record's `+0xA`), and every routine that places a piece walks exactly those nine cells, written out one by one:
- **Building the sphere:** `func_8008DBF8` tests the nine layout cells against the board at the target layer and rejects the spot if any cell it would cover is filled; only then do `func_8008BB24` and its relatives write the nine cells (nine calls to the write helper `func_8008BE58`), after a check that the piece would lie flat (the ROM's messages about a piece's height and "breaking a piece" come from these routines).
- **Dropping in play:** the landing layer is the top of the tallest column under the nine layout cells, plus one (`func_8008AB0C`), so a dropped piece always lands above everything under it and cannot overlap.

So any new shape that fits in 3 × 3 is checked correctly by the existing code. A larger shape would not be: the checks would only see its first nine cells, and it could be written over other bricks, exactly the risk to avoid.

**Limits built into the code:**
| Limit | Where | Value |
| --- | --- | --- |
| Footprint | the layout, the overlap test, placement, landing, the shadow | 3 × 3 |
| Cells per piece | the block value is 9 × material + cell number | at most 9 |
| Materials the game accepts | range checks in `func_8009423C`, `func_800CB6F4`, `func_800CD3B8` | 32 to 38 (32 to 37 the pieces, 38 the "?" piece) |
| Kinds in one stage | the slot table (`func_8008A5F0`, 40 slots, 4 per kind) | 10 |
| Kinds in the shape table before the fixed entries | `0x8014CBC8` | 22 |

**What a new kind would take, smallest first:**
1. **A new shape in an existing colour, within 3 × 3:** medium. Write one more shape record the way `func_800857E8` does (it could be BlockPlanet's own code after the game's builder runs), including the undecoded per-cell data, point a shape-table entry at it, raise the stage's kind count. Placement, overlap and landing work unchanged. The record's layout is now decoded (above), and there is no rotation to handle; the face textures follow the rule above (a single-cell piece or a cross still needs its tile chosen), and BlockPlanet's HUD needs a picture for the new shape ID.
2. **A new colour:** medium to large. All seven accepted materials are taken (38 is the "?" piece), so the range checks would need widening, and how the drawing picks a material's look (texture, colour) has not been read yet; BlockPlanet's own sphere renderer hooks may be the easier place to give it a look.
3. **A shape larger than 3 × 3, or with more than 9 cells:** large. Every footprint routine is written out for nine cells and would need replacing, and the block-value scheme itself (9 per material) would run into the next material's numbers, so it needs a new encoding.

## What the cell values mean

Each cell is a 16-bit number; the values seen are all **9 × kind + part**:

| Value | Kind | What is drawn |
| --- | --- | --- |
| 0 | — | Nothing (empty) |
| 9 | 1 | A single block filling a gap (the patterned single squares) |
| 63 | 7 | A special cell, two per layer below the top (what it is in play is not yet confirmed) |
| 81 to 89 | 9 | Training and Puzzle cores |
| 72 to 80 | 8 | Hide & Seek and Time Trial cores (pictures) |
| 90 to 93 | 10 | The ordinary core, a fixed repeating pattern |
| 288 to 335 | 32 to 37 | Piece blocks: six kinds, one shape and colour each; the part (0 to 8) says which cell of the piece this is |
| 423 | 47 | The stored Training and Puzzle layouts' blocks |

**Why the values jump (9, then 63, then 72 and up).** Values go up in steps of 9 per type, so the gaps are other types that the stages built in this session simply did not contain. From the [game-state notes](blockplanet-game-state.md): types 2 and 5 (values 18 and 45 up) are Hide & Seek's drills (the tip on the top layer, the shaft below), type 31 (279 up) is its tower, and the core and picture tiles use types 8 to 25; types 3, 4 and 6 (27, 36 and 54 up) have not been identified, and type 38 (342) is the "?" piece. 

Confirmed for 0, 9, 90 to 93 and the piece kinds from screenshots (two colours of piece per stage, as the logs show; each colour is one shape). There is no layer 9: the block table holds 9 layers, 0 to 8 ([game-state notes](blockplanet-game-state.md#layers-core-size-and-stacking-2026-11-02)), and what the research log shows as "layer 9" is the end of that table and the start of the next one (the face masks).

## What it allows

All of these are possible; none is built yet beyond the research switches.

- **Repeatable stages and shared seeds.** Forcing the two seeds gives the same sphere every time. A stage could be shared as "chapter, stage, seed": two 16-bit numbers, for example as an eight-letter code.
- **A daily stage.** Derive both seeds from the date; everyone gets the same sphere that day.
- **Fair races.** Two players already get identical spheres in one game; seeds make that true across machines too.
- **Designed spheres, layer by layer.** BlockPlanet can write cells straight into the grid once the game has built it, before the game derives its per-cell tables (that happens afterwards, in `func_800AF294`). `BLOCKPLANET_SPHERE_LAYOUT` does this today: a hand-drawn layer (a ring cut down to the core, single blocks elsewhere) was built and played. Single blocks (9), holes (0) and special cells (63) can go anywhere. Piece blocks need their parts placed in the piece's shape, which is not mapped yet.
- **Unusual stage types from edited settings.** Editing the record before the build makes stages the game never had, still built by the game's own builder. `BLOCKPLANET_STAGE_RECORD_EDIT` does this (one kind and all six kinds at once are tested); more layers: see below.
- **Per-layer seeds.** Because the number of draws per layer is fixed, BlockPlanet could restart the generator at each layer's first draw with a seed of its own, so each layer could be chosen or shared separately. Not built.

## Research switches

All are off by default and change nothing unless set (`src/graphics/sphere_build_research.cpp`, hooks in `config/recomp/tetrisphere.us.rev0.toml`):

| Switch | What it does |
| --- | --- |
| `BLOCKPLANET_SPHERE_RESEARCH=1` | Logs `sphere_seed` (both seeds), `level_build_begin` (mode, level, generator state) and `level_build_end` (draw counts by range, the settings record as read, and each layer's hash and value counts). |
| `BLOCKPLANET_SPHERE_SEED=n` | Uses *n* (0 to 65,535) as the sphere seed. |
| `BLOCKPLANET_PIECE_SEED=n` | Uses *n* as player 1's piece seed (and its copy). |
| `BLOCKPLANET_RNG_LOG=1` | Also logs every draw made during the build (range and state) and each layer's fill every 600 draws. |
| `BLOCKPLANET_STAGE_RECORD_EDIT="2=1,…"` | Changes bytes of the stage's settings record (byte=value) after the game reads it and before it unpacks it (hook in `func_8002A870`). |
| `BLOCKPLANET_RECORD_SURVEY=N` | The first time a record is loaded, also loads records 0 to N − 1 with the game's own loader and logs each (`record_survey`), then puts the stage's record back. |
| `BLOCKPLANET_PLAY_LOG=1` | During play, logs player 1's hearts, each layer's fill and the tallest column whenever they change. `BLOCKPLANET_KEEP_HEARTS=1` also keeps the hearts at 3. |
| `BLOCKPLANET_SPHERE_LAYOUT=file` | Writes hand-made layers over the built sphere. The file has `layer N` lines (1 to 8), each followed by 32 rows of 32 cells: `.` empty, `#` single block, `*` special cell, or 32 numbers (raw values). Lines starting with `;` are comments. |

The research logs (records as read, every draw) go to the game's own console output under `.local/` and are never committed.

## Evidence

Runs on 2026-11-18, Linux build under Xvfb with lavapipe, started with the harness (`poke cmd:chapter:C:L` or `cmd:launch:MODE:C:L`, then Return):

- Chapter 10, stage 1, both seeds forced (1,234 and 777), three starts: identical layer hashes and identical screenshots of the sphere. With the sphere seed changed to 4,321: every layer different.
- Chapter 1, sphere seed forced to 1,234 but the piece seed left free: the same number of draws and the same final generator state, but a different layer 1. That is how the piece seed was found.
- Free starts: seven different seeds drawn from identical scripted input.
- Layer fill order: chapter 10 with `BLOCKPLANET_RNG_LOG=1` (above).
- Modes: Rescue (chapters 1, 3 and 10), Lines (1 and 5), Time Trial, VS CPU, Hide & Seek, Training, Puzzle and the attract demo, one build each, by `level_build_end`.
- Hand-made layer: chapter 1 with a ring layout; the ring shows down to the core in play.

## Still open

- What the special cells (63) are in play, and the bit-to-kind link of byte 11.
- The meaning of record bytes 3 to 8, 10 and 12.
- Which sphere seeds give an easier or harder sphere: that needs a measure of difficulty (for example, the cells to clear to reach the core) and either many builds in the game or a copy of the builder.
- The part numbering of each piece shape (needed to draw piece blocks by hand).
- Hide & Seek's extra draws and how it picks its records; modes 7, 11 and 12.
