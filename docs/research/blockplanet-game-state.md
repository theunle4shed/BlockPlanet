# Game state for trophy events (BlockPlanet)

Guest addresses used by `GameEventWatcher` (`include/tetrisphere/game_event_watcher.h`) to turn play into trophy events. NTPE rev0 only. Found on 2026-11-01 by reading the HUD code in the generated C and by recording memory every second graphics task during a scripted Rescue game (drops every 3 seconds, hearts lost, game over, retry). Blocksphere's [m3-state-map.md](m3-state-map.md) gave the player context and score.

RDRAM is stored as 32-bit host-order words, so a byte address is read at `address ^ 3` and a halfword at `address ^ 2`.

## Confirmed

| Value | Address | Type | Evidence |
| --- | --- | --- | --- |
| Scene | `0x800E121C` | s16 | -1 for the whole of gameplay, including retries. Other values in menus and transitions. |
| Game mode | `0x800E44A8` | s16 | During play: 1 Rescue, 2 Time Trial, 3 Training, 4 Puzzle, 5 Hide & Seek, 6 VS CPU (one recording each, read with the screenshots). Menus also show 2, so read it only during play. |
| P1 context | `0x80110220` | base | Blocksphere's map. P2 is `0x80113488`. |
| Score | context `+0x2AF8` (`0x80112D18`) | s32 | 0 at game start, 1040 after a scoring drop. The HUD shows ten times this value ("10 400"). The HUD's animated copy is `0x800E241C`. |
| Hearts left | context `+0x2D7E` (`0x80112F9E`) | s16 | 3 at game start, then 2, 1, and -1 when the last heart is lost (game over). Refills to 3 at each retry, in the same frame play begins. |
| Hearts shown | context `+0x30FC` | s16 | Follows hearts left. The HUD animates a heart when the two differ (`func_80078BA8`). |
| Heart states | context `+0x2FFC + 0x20 × i`, i = 0..2 | u8 | 0 heart, 1 turning, 2 skull. |
| Current piece | `0x801127FB` | u8 | Piece type; 8 for about 0.1 s on every drop. |
| Retries | `0x800E088F` | u8 | Increments on each TRY AGAIN, not on the first game. |
| Chapter | `0x800E44A0` | s16 | Rescue and Hide & Seek chapter, 0-based. Writing 4 during play showed "5:1" in the HUD. Writing it before the level loads builds that chapter's sphere (chapter 10, 15 and 20 tested). |
| Level in chapter | context `+0x23BA` (`0x801125DA`) | s16 | Level within the chapter, 0-based; the puzzle number in Puzzle. Advanced from 0 to 1 when level 1:1 was cleared. |
| Time Trial clock | context `+0x2B0C` | s32 | Blocksphere's candidate. Counts down from 18 059 (1/60 s, about five minutes) in mode 2. |

## How levels are built

The P1 block grid at `0x80116BF0` (Blocksphere's map) holds 32×32 cells of s16 per layer, 2048 bytes per layer.

- **Layer 0 is the core:** a fixed repeating tile pattern (cell values 0x5A to 0x5D).
- **The playfield layers are generated randomly on each start.** Two starts of chapter 1 left layer 1 different in 887 of 1024 cells, with similar mixes of values.
- **The chapter sets the depth:** chapter 1 filled 2 layers (core and one playfield layer) and chapter 10 filled 6.
- Layer 9 held identical values in both runs (384 cells of 0x1010).

So levels are generated, not stored designs. Custom levels are possible in principle: BlockPlanet could write a designed grid before play starts, as `--start-chapter` does for the chapter. The meaning of each cell value (piece id, colour, crystal) still needs mapping.

## Inferred, not yet confirmed

| Value | Evidence |
| --- | --- |
| `0x801125DA` = puzzle number | The HUD draws this number only in mode 4 (Puzzle), in the level badge. |
| Catch-up cap applies to Puzzle | The cap is for mode 4. See [frame pacing findings](blockplanet-frame-pacing.md). |
| Puzzle solved = puzzle number advancing | Same field as the Rescue level. Not yet seen in play. |
| Highest chapter | Chapter 20 loads a sphere; the real number of chapters is not known yet. |

## Events the watcher reports

| Event | Detection |
| --- | --- |
| `mode_started` | Entering play with 3 hearts and score 0, or hearts refilling to 3 during play. |
| `piece_placed` | Current piece changes to 8. |
| `life_lost` | Hearts left decreases. |
| `game_over` | Hearts left becomes -1, outside VS. Reports `score` as the HUD shows it. |
| `level_completed` | In Rescue or Hide & Seek, chapter or level higher than at the last moment of play. Reports `chapter` and `level` (1-based), `score` and `hearts_lost` during that level. Seen in play: clearing 1:1. |
| `chapter_completed` | The chapter number went up. `final` is always 0 until the last chapter is known. |
| `puzzle_solved` | In Puzzle, the puzzle number went up. Not yet seen in play. |
| `vs_round` | In VS CPU, P1's hearts reaching -1 is a `loss`; P2's (context `0x80113488 + 0x2D7E`) is a `win`. The P1 side was recorded: hearts run out and refill each round. The P2 side is inferred from the same layout. |

Mode names are reported for modes 1 to 6. Practice, two-player VS and Lines are not identified yet and report events without a mode. Puzzle keeps hearts at 3, so it never reports `life_lost` or `game_over`.

## Still to find

- Mode numbers for Practice, two-player VS and Lines.
- The number of chapters, to report `final` on the last one.
- Combo size and multiplier.
- Magic use and magic type.
- Puzzle drops and slides.
- Game over for modes without hearts: Puzzle, and Time Trial when the clock runs out.

## How the sphere is drawn (culling research, in progress)

Started 2026-11-02 to understand why blocks near the edge of the view go missing or show inner layers, which the owner confirmed also happens on real hardware.

- **Cursor:** context `+0x2B1A` (x) and `+0x2B1E` (y), 0..31 on the grid, with copies 8 bytes apart. The sphere turns to keep the cursor centred. `0x8015F88A`/`0x8015F88E` hold cursor + 7, the origin of a 13×13 neighbourhood used by the drop and shadow logic (`func_8009AB60`, `func_8009AAA8`), not by rendering.
- **The sphere's block geometry is a fixed amount every frame.** Logging RT64 vertex loads in gameplay showed 67 loads of 16 vertices from a per-frame buffer (`0x80254000` to `0x80258180` in the sampled frames) in every frame, while rotating as well. The number does not depend on the view.
- **Writer:** a hardware watchpoint on that buffer stopped in `func_8009C330`, called by `func_8009E630` ← `func_800A348C` ← `func_800CD3B8` ← `func_80070118` ← `func_800CCE7C`.
- `func_800A348C` calls about ten sphere drawing passes once each (`func_8009E630`, `func_8009EB28`, `func_8009F024`, `func_8009F520`, `func_8009FA20`, `func_800A0ED8`, `func_800A18F4`, `func_800A1E58`, `func_800A2374`, `func_800A2E88`).
- `func_8009E630` loops 8 rows (counter `0x8015F836`) over a column span held in `0x8015EDB2`..`0x8015EDAE`. Each row starts with `func_8009D90C`, which reads a **precomputed mesh table** (pointer at `0x800E44E4`; rows of 0xC0 bytes, 12-byte entries) and interpolates vertex positions.

**Working conclusion:** the game draws a fixed patch of the sphere's surface around the view centre and maps nearby blocks onto it, rather than drawing the whole sphere and hiding what is out of view. Blocks outside the patch have nowhere to be drawn, which explains the gaps and stray walls near the edge. Still to confirm: the patch size in cells, how cells map onto it, and whether the mesh table can be enlarged (memory, and the fixed vertex buffer size) to cover the whole visible hemisphere or the whole sphere for a rear camera.
