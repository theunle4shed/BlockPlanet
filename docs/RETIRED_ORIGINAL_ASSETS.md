# Original pieces BlockPlanet no longer shows

Pictures and drawing from the original game that BlockPlanet hides or replaces, with what replaces them and the switch that brings them back, if there is one. Kept for the record: what was retired, and why.

**Nothing here is in BlockPlanet's install.** BlockPlanet ships no game data. Every picture is in the player's own ROM and is loaded by the game's code into memory as it runs. BlockPlanet just doesn't draw the retired ones. Nothing is extracted to disk, apart from the developers' asset catalogue in `.local/`, which is never shipped. The ROM itself stays whole: the game's code expects every file in it, and it is checked as the player's own copy.

| Original | Where | Replaced by | Since | Bring back |
|---|---|---|---|---|
| Title picture: logo, robots, oval and copyright lines (a 270 × 205 picture, with a 280 × 188 shadow) | Title screen | The 3D letters, ring, Turbine, Wheels and Bombs, and the notice as sharp text | Title rebuild | — |
| Pixel fonts 0 (menus) and 1 (cards) | Menus such as Try Again, the cards, Training | Lato, laid out by BlockPlanet, in proper case with button symbols | 2026-11-07 | `BLOCKPLANET_ORIGINAL_TEXT=1` |
| Button glyphs painted into the Training font (`training_prompts.cpp`) | Training text | Not needed while the text is replaced: the panel draws button symbols | 2026-11-07 | Used again with `BLOCKPLANET_ORIGINAL_TEXT=1` |
| Training's speech bubble (150 × 115) | Training | BlockPlanet's speech panel: the menus' style, with a tail at Wheels | 2026-11-07 | `BLOCKPLANET_ORIGINAL_TEXT=1` |
| "▼ more ▼" and "◀ watch" prompts (62 × 14, 50 × 14) | Training | The panel's hint: the A symbol and "More", or "Watch" | 2026-11-07 | `BLOCKPLANET_ORIGINAL_TEXT=1` |
| N64 controller picture (65 × 59 and 50 × 28), its lit buttons (11 × 11, 8 × 8) and the arrows pointing at them | Training | Nothing: the speech names the player's own buttons | 2026-11-07 | `BLOCKPLANET_ORIGINAL_TEXT=1` |
| Rules and goal card box (203 × 143) and its "▼ more ▼" | Hide & Seek, Lines, VS | BlockPlanet's card panel, without a tail, with the whole text at once | 2026-11-07 | `BLOCKPLANET_ORIGINAL_TEXT=1` |
| The original HUD while a card is up | Hide & Seek, Lines, VS | BlockPlanet's HUD stays up | 2026-11-07 | `BLOCKPLANET_ORIGINAL_TEXT=1` |
| The original HUD (2D parts drawn by `func_80075CD8`, and the held magic's model) | Story, Hide & Seek, Puzzle, Lines, VS | BlockPlanet's HUD panels | HUD work | The original HUD shows where BlockPlanet's has no layout yet (Training for now) |
| Pause screen's white line, menu text, button hints and instructor | Pause | BlockPlanet's pause panel (the game's pause background is kept) | Pause work | — |

## Still original, and planned to go

- **Training's original HUD** (hearts, 1x, magic bowl, the next piece in the corner): it will be replaced by Story's HUD at the bottom of the screen, shown as each element is introduced (`docs/TRAINING_REDESIGN.md`).
- **Text painted into pictures**: the game-over hint and Hide & Seek's short hints (`docs/research/card-text-audit.md`).
- **Font 2**: not yet identified.
