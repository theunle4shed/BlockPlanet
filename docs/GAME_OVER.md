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

