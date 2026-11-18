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
| 11 | A bit mask over the six piece kinds: which kinds (shape and colour) the stage uses. In every Rescue record it has as many bits set as byte 2. | Inferred (the exact bit-to-kind link is not yet clear) |
| 3 to 8, 12 | Further settings; byte 6 holds three on/off flags, byte 12 a count of entries that follow. Hide & Seek records use bytes 7 and 8. | Unknown |

**Rescue's stages, all 100 (surveyed 2026-11-18).** `BLOCKPLANET_RECORD_SURVEY` loaded each record with the game's own loader (records 0 and 90 matched what chapter 1 and chapter 10 load in play):

- **Piece kinds:** 2 in 37 stages, 3 in 61, 4 in 2. Episodes 1 and 2 always use 2. Kinds become 3 from Episode 3 (stages 5 and 8 first), and are always 3 in Episodes 5 to 9. Episode 10 starts with 2 and reaches **4, the most in Rescue, in its stages 8 and 10**. Stage 10-8 was started to check: its layers hold four kinds (33, 34, 35 and 37), drawn in four colours, and its piece draws use range 16.
- **Layers:** 1 (only the first stage), 2 in 9 stages, 3 in 15, 4 in 41, 5 in 23 and 6 in 11 (Episode 7's stages 8 and 10, and Episode 10 from stage 2).

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

1. **Let pieces rest in layer 8.** Pieces never stayed in layer 8 in these tests, although the board has it and the game draws it with its own pass (`func_8009FA20`). If one check in the landing code refuses it, a hook there gives one more layer of room. Small to medium; the landing check has to be found first (`drop.c` area).
2. **A taller board.** Move the four tables to the free upper 4 MB (their pointers are set per player in `func_800AF294`, so they can point anywhere; about 7 KB per extra layer), change every layer bound (hooks cannot change a constant inside an instruction, so this means rewriting those spots in the generated code, as `tools/recomp/generate.py` already does for other patches, or replacing the board helpers with BlockPlanet's own), extend the mesh builder's layer steps, and pull the camera back (higher layers come closer to it). The column map's 4-bit layer numbers allow up to 15 layers. Large.

These describe drops in Rescue with the heart rule as seen; the code that decides the heart loss (`drop.c` in the ROM's messages, around `func_8009423C`) is not read yet.

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
