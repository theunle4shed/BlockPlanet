# Failing a game: one approach for every mode (2026-11-15)

The owner (2026-11-15): failing should look and work the same in every mode, whether hearts run out or a puzzle is failed; in all of them **the black shell encases the sphere**. The shell is BlockPlanet's own (`src/graphics/game_over_shell.cpp`), still wrong on the owner's PC (ROADMAP: to be replaced with a fresh design, not patched).

## How each mode fails today

| Mode | How you fail | What the game does | BlockPlanet today |
|---|---|---|---|
| Rescue, Time Attack, Hide & Seek, Lines | Hearts run out | Dark bricks stream out past the camera, then the GAME OVER letters; then its menu: Try Again, Exit, Training | The shell over the sphere (when hearts are −1), then the Game Over panel on a red grid |
| Score Attack | The clock reaches 0:00 | BlockPlanet sets the hearts to 0, so the game's game over follows | As above |
| Puzzle | A drop or slide that leaves the puzzle unsolvable (out of drops and slides) | Takes a heart (3 to 2, not to −1), the same dark bricks, then the same menu with "You may want to try Training if you haven't already" | **Fixed 2026-11-15:** the menu was hidden under the pause grid (a blank grid); now the Game Over panel. The shell does not show (it waits for hearts −1) |
| Minefield | Opening a mine | (BlockPlanet's own mode) | Mines turn red, every other cube goes, the stage ends and goes back to its menu; no Game Over panel |
| Casual | Cannot fail | — | — |
| Training | Cannot fail (lessons) | — | — |
| Versus | Losing a match | (paused: one-player modes first) | "Match Over" panel |

## Plan (for the owner)

1. **One failure sequence:** the black shell closes over the sphere, then the Game Over panel (red grid) with what was reached, then Try Again, Training and Exit.
2. **Every mode that can fail uses it:** Rescue and its variants, Hide & Seek, Lines, Puzzle and Minefield.
3. **The shell, redesigned** (fresh, not patched): how it closes and what it looks like are the owner's call.

## Built (2026-11-15)

- **Puzzle:** a failed puzzle (the game takes a heart and shows its own menu) now opens the Game Over panel (Try Again, Training, Exit); it was a blank grid. The game's own dark bricks close in first.
- **Minefield** (the owner: "show the mine locations and that they've missed a mine… and add the black shell from the standard game over"): when a mine is opened, every other cube goes and the mines stay: **the mine opened flashes red and yellow, mines not flagged are red, mines flagged are green, and flags with no mine under them purple**, with the numbers still on the core. Three seconds later the black shell closes over the sphere (BlockPlanet's, `set_game_over_shell_forced`), then the Game Over panel ("Minefield · Mini"): New Field and Exit. The game's own game over is not used here: in Rescue's mode it goes on to its high-score table, which waits for a name.
- **Research:** `poke cmd:research_minefield_lose` flags two mines and a cell with none, then opens a mine.
- **Found:** after the game's own game over in Rescue, with a score table it can enter (a new profile's, empty), the game shows the table (RESCUE and five rows) and waits for a name before its menu, under BlockPlanet's red grid. Seen when forcing game overs in the test setup; to check in real play.

## The shell, second version (the owner's design, 2026-11-15)

The owner's report on the first: the shell clipped the sphere's cells and its 4 × 4 blocks were too big. The design: on a game over the sphere can no longer be turned; black blocks come in at the height of the tallest block in that game (Minefield: layer 2; a deep Rescue stage: more), starting from behind the sphere and closing in from the sides until the whole sphere is covered, each block spinning and growing into its place.

Built (`src/graphics/game_over_shell.cpp`):
- One block per board cell (`BLOCKPLANET_GAMEOVER_SHELL_CELLS=4` gives the first version's).
- Every block is a solid column from the core to one height: the tallest column in view when the game ended, so the shell is smooth and nothing pokes through.
- Blocks start at the rim and sweep in to the middle over 1.2 s (a little scatter so it reads as many blocks). The owner's refinement: each block is already in its place, small and invisible, and fades in while growing to its full size over 0.6 s (a translucent render mode, alpha from the primitive colour, the sphere's own put back after).
- Minefield: turning is frozen while the shell is up; the shell is only on while that field is lost (it once carried over into a new field).
- The loading cover says "Minefield" for a new field (it said "Rescue"), and a Rescue variant's own name.

Still to do: the game's own game over sequence (dark bricks streaming past the camera, the GAME OVER letters, the camera pull-away) still plays in the modes with hearts, and the shell is drawn only while the sphere is in view; replacing that sequence needs its routines found. Starting behind the sphere is not needed (the owner: the sphere is never turned to see the back under the shell).

## Research: the game's own game over sequence (2026-11-15)

Recorded in Rescue 1:1 (hearts set to 0 and the drop timer near its limit, so the next heart lost is the last):

1. About 0.6 s after the last heart: the board's bricks turn grey and dark bricks start streaming out from the sphere past the camera.
2. About 1.2 s: the camera pushes away from the sphere and the large 3D **GAME OVER** letters fly in, grey turning red.
3. About 2.5 s: everything fades to black (the bricks still streaming), then the game's menu (BlockPlanet's red grid and Game Over panel).

**The letters are their own LightWave scene, `0x801EED20`, with eight objects** (the eight letters, first drawn at the moment they appear; parts lists `0x801DC8EC` to `0x801DC99C`). `BLOCKPLANET_OBJECT_ONLY=801EED20:0,1,2,3` drew only some letters (G, A, E, E), and `801EED20:99` none at all, with nothing else changed: so **the letters can be isolated** and hidden, or drawn by BlockPlanet itself at its own place and time, as the title's 3D letters already are (`blockplanet_title_object`).

**The dark bricks** are not in that scene (they stayed with every letter hidden) and not among the models logged (`BLOCKPLANET_MODEL_LOG`); the routine that draws them is still to find. **The camera's pull-away** is also still to find (ROADMAP already notes it).

Recommendation: reuse the game's own letters (they are the original's art, drawn by the game from the ROM at run time, so nothing is copied into BlockPlanet), shown after BlockPlanet's shell has closed, centred over the sphere; hide the dark bricks and hold the camera on the sphere once their routines are found. Rebuilding the letters as new models would be BlockPlanet's own art: more work, and only needed if the owner wants a different look.

## Research: holding back the game's own game over (2026-11-15, not working yet)

Goal (the owner's sequence): last heart lost → the sphere freezes → BlockPlanet's shell fades in → the game's GAME OVER letters → the Game Over panel.

Tried, then removed (the build is as before):
- **Holding the hearts check.** `func_800CD3B8` checks player 1's hearts each frame (`lh $t8, 0x2D7E($s2)` at `0x800CEDE8`, branch at `0x800CEDF0`); at 0 it sets them to −1, `+0x23DE` = 60 and calls `func_800ADBC0` (`+0x5A4` = 1, `+0x2414` = `0x1838`). Making `$t8` non-zero there held the hearts at 0 for 2.4 s, but **the sphere still pulled away and the GAME OVER letters still came** after about 1 s.
- **Holding the last heart's loss.** `func_800940F0` (a0 = the player's context) loses a heart: it calls `func_800ADC44`, resets the drop state and decrements `+0x2D7E`. Its callers: `func_800ACB94` (`0x800AD5F0`, the drop timer running out, after setting `+0x2414` = `0x429A`), `func_800CD3B8` (`0x800CDB8C` after `func_8008A6E4`, and `0x800CDE24`), `func_8009423C` (`0x80094BF4`). Skipping it for the last heart (hearts kept at 1, shown on the HUD) still let the game over begin about 1 s later.

So the game over is started by something other than the hearts value and the heart-loss routine, probably in the caller's own path (the bad drop in `func_800CD3B8` or what `func_8008A6E4` sets). Next: watch which player-context fields change in the second after the last bad drop (a memory diff each frame), to find the flag the game over sequence runs from.

## Found and built: holding the game over (2026-11-15)

A memory diff around the last bad drop (menu commands `research_diff_start`, then `research_diff_mark`: bytes that change by themselves for 2 s are ignored, then every other byte's first change is logged for 3 s with its time) showed the game over starting in the same frame as the last heart's loss: hearts 1 → −1, `+0x5A4` = 1, `+0x23DE` = 60. **Skipping the loss of the last heart (`func_800940F0` with one heart left) holds the game over off completely** (play just goes on). The earlier attempts failed only because BlockPlanet's Minefield code switched the shell off every frame.

Built: the last heart's loss is held back (`blockplanet_game_over_hold_loss`), the sphere freezes (no input) and BlockPlanet's shell closes over it with the HUD still up; after 2.4 s the held loss is run on the game's own thread (`blockplanet_game_over_replay`, before the hearts check in `func_800CD3B8`), and the game's own game over follows (its camera pull-away, its dark bricks and its GAME OVER letters), then the Game Over panel. Not in Versus, Puzzle or Training; `BLOCKPLANET_GAMEOVER_SHELL=0` leaves the game's own game over alone.

Still to do: keep the camera on the covered sphere and hide the game's dark bricks during its game over, so only the letters show over the shell; the piece in hand still showed through the shell (fixed 2026-11-16: its shadow, the cursor marker `func_8009AB60`, is not drawn while the shell is up; a piece in flight can still show above it). The piece still showing was the one just dropped: the game launches it before checking the drop (the check is when it lands, as in the original, where the camera pulled away at once and hid it), and it lands after the shell has begun, a layer above the rest. Fixed: the shell's height now follows the tallest column every frame.

## The camera back to its starting point (2026-11-16)

The owner: the camera should go back to its starting distance for the shell, as some modes close it in (the drop timer, for one). The camera's distance for a frame is worked out in `func_800CD3B8` just before `0x800CEFE0`: player context `+0x2420` (from the drop timer's push, `+0x2416`, and `+0x2454`) plus `+0x243C`; writing `+0x2420` elsewhere is overwritten each frame. BlockPlanet hooks `0x800CEFE0` (`blockplanet_game_over_camera`): in calm play (drop timer `+0x2B10` below 100) it notes the resting distance; while the game over is held it eases the distance back to it, a quarter of the way each frame, and the shell starts once it is there (at most a second after the hold began). Checked in Rescue 1:1 with the drop timer run out (closed in to about 1,750 against a resting 2,423): the sphere eases back to its normal size, then the shell closes.

Research: the menu command `research_drop_timer:N` sets the drop timer N units short of its limit (6,000).

## The game's own pull-away held off (2026-11-16)

During its game over the game sets player context `+0x2414` to `0x1838` (`func_800CD3B8` at `0x800CF1A8`, while `+0x5A4` is set), which drives the camera far out: its pull-away. BlockPlanet's camera hook now also keeps the camera at its resting distance while player 1's hearts are −1 (`BLOCKPLANET_GAMEOVER_PULL=1` lets the pull-away run), so the GAME OVER letters fly in over the covered sphere. The dark bricks are drawn apart from the sphere (they stayed with `BLOCKPLANET_HIDE_SPHERE=1`), and not by the routines that read the piece's floats (`func_8008A2BC`, `func_800A4AF0`, `func_800A5BB4`, skipped in turn); still to find.

## The dark bricks found and hidden (2026-11-16)

Skipping the routines that call the block face code (`func_8009C330`) one at a time during a game over (`BLOCKPLANET_SKIP_FN`, research only, now removed): with **`func_800BB3D0`** skipped no dark bricks fly at all, with everything else unchanged. It is now skipped while BlockPlanet's shell covers the sphere (held, or hearts at −1; not in Versus; `BLOCKPLANET_GAMEOVER_SHELL=0` keeps the game's own). The sequence is now the owner's: the sphere freezes, the camera eases back, the shell closes, the GAME OVER letters fly in over the covered sphere, then the Game Over panel. Still seen: a small piece left of the sphere for a moment as the shell closes (to look at).

