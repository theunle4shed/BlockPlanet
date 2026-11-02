# Level data (BlockPlanet)

How Tetrisphere stores the settings for each stage, found on 2026-11-02. The full per-level table is derived from original game data, so it is not stored in Git. Generate it from your own ROM:

```
python3 -m tools.analysis.level_table .local/m0/tetrisphere-us.z64 > .local/analysis/rescue-levels.md
python3 -m tools.analysis.level_table .local/m0/tetrisphere-us.z64 --all --csv > .local/analysis/all-level-records.csv
```

## Where it is

- **Offset table** at ROM `0x7D4A64`: 347 entries of a 16-bit big-endian offset followed by two zero bytes.
- **Records** at ROM `0x7D4A72 + offset` in the tool's numbering (the first Rescue record is at `0x7D4FD0`). Most records are 20 bytes. Records whose flags include `0x10` or `0x20` carry a goal text after byte 16, ending with a zero byte. `-0` in the text is a line break.
- **How the game reads it:** `func_8002A870(index)` copies record *index* from the file at ROM `0x7D4A70` into a buffer at `0x8013D270`, then `func_80088914` unpacks it. The buffer starts at the tool's byte 3, so the game's field offsets are the tool's minus 3. The tool keeps its own numbering for now.

## Sections

| Entries | Contents | Evidence |
| --- | --- | --- |
| 0–3 | Header or sub-tables, not records | Values do not follow the record format. |
| 4–153 | **Rescue: 15 chapters × 10 levels.** Entry = 4 + (chapter − 1) × 10 + (level − 1). | Layer counts match the boards of chapters 1–15 level 1, and chapter 1 level 9 (set through memory) built the 3 layers its record specifies. |
| 154–160 | Seven records in the Rescue format with group byte 7 and byte 12 rising from 4 to 10 | Starting "chapter 16" loads entry 154. Starting chapters 17 and 18 crashed and chapters 21 and above built broken spheres, because they read into the next section. So the game has 15 regular Rescue chapters; what these seven records are for is not known yet. |
| 161–195 | Hide & Seek and goal levels, with text such as "UNCOVER MULTIPLE CORES FOR A HUGE BONUS", "LOOK UNDER THE TOWERS TO FIND A CORE", "FIND ALL 4 PARTS OF THE CORE" and "MAKE A BRICK FALL TO THE CORE" | Goal text and the extra flags. Not yet matched to stages in play. |
| 196–345 | Further records with non-zero bytes 9 and 10 and group values 0 to 10 | Probably other modes (Puzzle, VS, Time Trial). Not yet identified. |

## Record fields (20-byte format)

| Byte | Meaning | Confidence |
| --- | --- | --- |
| 2 | Flags. `0x0E` normal; `0x10`/`0x20` add goal text; other bits seen in goal levels | Partly known |
| 3 | **Playfield layers** above the core | Confirmed against 16 boards |
| 4 | 0 in Rescue, `0x0F` in many goal levels | Unknown |
| 5 | **Number of block colours** (2 to 4) | Matches the colours seen on the boards |
| 9, 10 | Zero in Rescue; set in later sections | Unknown |
| 12 | Rises at milestone levels (often levels 5 and 10) and with chapter, 2 to 4 in Rescue | Becomes a size of 2 × value (see below); its meaning in play is not confirmed |
| 13 | Group: 2 for Rescue, 7 for entries 154–160 and Hide & Seek, other values later | Partly known |
| 14 | **Colour set**: bit *k* selects block type 32 + *k* | Mostly confirmed: the generated board matched exactly in 11 of 16 chapters; in the rest one colour came out as a neighbouring one, so the game varies the set slightly |
| others | Zero in Rescue | — |

## Where the fields go (from `func_80088914`)

Using the tool's byte numbers:

| Byte | Copied to | Notes |
| --- | --- | --- |
| 3 (layers) | `0x8013D17C` | Level structure at `0x8013D170` |
| 4 | `0x8013D17E` | |
| 5 (colours) | `0x8013D186` | |
| 14 (colour set) | `0x8013D188` onwards | For each set bit, one entry of a six-entry colour table at `0x800E4508` (pointers `0x80127CF0` … `0x8012A1F8`) |
| 13 (group) | `0x8013D248` | Through a lookup table at `0x800E4520` that adds 1 |
| 6–9 | `0x8013D24A`–`0x8013D24E`, flags at `0x8013D250`–`0x8013D254` | Byte 9 is a set of flags |
| 12 | `0x8013D266`, then the player context `+0x23E2`; twice the value, kept between 2 and 10, goes to `+0x23E4` | In VS CPU the game adds a chapter-based amount first. `+0x23E4` = 4, 6 or 8 also sets `0x8015F4F0` to 0, 1 or 2 |
| 15, 16 | `0x8013D260`, `0x8013D264`, `0x8013D25C` | |

**What `+0x23E4` does is not confirmed.** `func_800AC8E8` searches the board for a square region whose width and height are `+0x23E4` and compares the result with it, so it is a size, possibly the core opening a level needs (the Practice menu offers "layers" and "core area" settings). A test that forced it to 2 or 10 during play did not change whether chapter 1 level 1 completed within 20 drops, but the value may only be read when a level starts, so the test does not rule this out.

## Block types on the board

Cell value ÷ 9 is the block type (`func_8008A7EC`); the remainder distinguishes cells within a type.

| Type | Seen | Meaning |
| --- | --- | --- |
| 10 | The whole bottom layer, every stage | The core |
| 32–37 | The playfield, two to four of them per stage | Block colours, chosen by the colour set |
| 1 | About a quarter of every playfield layer | Unknown |
| 7 | Two per playfield layer from chapter 1 level 2 on, never in the top layer | A special block, probably buried bonuses; not confirmed |

The playfield is generated randomly at each start from these settings (see [blockplanet-game-state.md](blockplanet-game-state.md#how-levels-are-built)).

## How difficulty scales in Rescue

| Chapter | Playfield layers | Colours | Byte 12 |
| --- | --- | --- | --- |
| 1 | 1–3 | 2 | 2–3 |
| 2 | 2–4 | 2 | 2–3 |
| 3 | 3–5 | 2–3 | 2–4 |
| 4 | 3–4 | 2–3 | 2–4 |
| 5 | 4–5 | 3 | 3–4 |
| 6 | 4–5 | 3 | 3–4 |
| 7 | 4–6 | 3 | 2–3 |
| 8 | 4 | 3 | 2–4 |
| 9 | 5 | 3 | 2–4 |
| 10 | 5–6 | 2–4 | 2–4 |
| 11 | 1–3 | 2–3 | 2–3 |
| 12 | 2–4 | 2–3 | 2–3 |
| 13 | 2–4 | 2–3 | 2–4 |
| 14 | 3–5 | 2–3 | 2–4 |
| 15 | 4–6 | 2–3 | 3–4 |

- **Depth** grows through chapters 1 to 10, then drops back at chapter 11 and climbs again. Chapters 11 to 15 look like a second cycle, harder in some other way (byte 12, or settings not yet found).
- **Colours** move from two to three around chapter 5. Fewer colours make combos easier; more colours make them harder.
- **Byte 12** steps up at milestone levels within chapters.

## Not found yet

- **Fall speed and timers.** Not in the fields followed so far. Byte 12 now looks like a size rather than a speed. Values in the player context around `+0x2DFC`–`+0x2E08` read 1203, 601 or 467 (20, 10 or about 8 seconds at 60 per second), but they do not follow any record field, so they may come from chapter rules in code.
- **Levels per chapter in play.** The data has 10 levels per chapter, but `func_80089D04` returns 5 for Rescue (3 for Hide & Seek and VS CPU). It may count something else, such as levels between rewards. Needs a playthrough of a full chapter.
- **Core size.** The core always fills the whole bottom layer; "uncover multiple cores" goals suggest Hide & Seek uses separate core objects.
- What byte 12's size means in play, and what the type 1 and type 7 blocks are. Next steps: set `+0x23E4` before the level starts (as `--start-chapter` does for the chapter) and repeat the completion test several times; and follow the readers of the level structure at `0x8013D170` and of `0x8013D248`.
