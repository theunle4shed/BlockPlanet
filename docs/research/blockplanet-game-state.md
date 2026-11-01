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

## Inferred, not yet confirmed

| Value | Evidence |
| --- | --- |
| `0x801125DA` = puzzle number | The HUD draws this number only in mode 4 (Puzzle), in the level badge. |
| Catch-up cap applies to Puzzle | The cap is for mode 4. See [frame pacing findings](blockplanet-frame-pacing.md). |

## Events the watcher reports

| Event | Detection |
| --- | --- |
| `mode_started` | Entering play with 3 hearts and score 0, or hearts refilling to 3 during play. |
| `piece_placed` | Current piece changes to 8. |
| `life_lost` | Hearts left decreases. |
| `game_over` | Hearts left becomes -1, outside VS. Reports `score` as the HUD shows it. |
| `vs_round` | In VS CPU, P1's hearts reaching -1 is a `loss`; P2's (context `0x80113488 + 0x2D7E`) is a `win`. The P1 side was recorded: hearts run out and refill each round. The P2 side is inferred from the same layout. |

Mode names are reported for modes 1 to 6. Practice, two-player VS and Lines are not identified yet and report events without a mode. Puzzle keeps hearts at 3, so it never reports `life_lost` or `game_over`.

## Still to find

- Mode numbers for Practice, two-player VS and Lines.
- Level and chapter numbers, and level and chapter completion. The HUD formats the level with `sprintf` (`func_800D3184`, format at `0x800EE060`) before `0x8007691C`.
- Combo size and multiplier.
- Magic use and magic type.
- VS round result.
- Puzzle solved, drops and slides.
- Game over for modes without hearts: Puzzle, and Time Trial when the clock runs out.
