# What's new in BlockPlanet (draft)

A draft of the release notes for BlockPlanet's first release, grouped by area. Each line says what changed and, where it helps, why. The full record, with dates and technical detail, is `CHANGES-SINCE-BLOCKSPHERE.md`. BlockPlanet continues [Blocksphere](https://github.com/djultra64/Blocksphere) by djultra64.

Draft, 2026-11-07: still to check against the owner's Windows test; features marked *(coming later)* are shown in the menus but not playable yet.

## Menus

- **New menus**, built for big screens and controllers: Continue, Solo, Local, Online *(coming later)*, Profiles, Options, Credits.
- **Choose who you play with, then the game, then how**: Solo lists Story, Hide & Seek, Puzzle, Lines and Multiphasic *(coming later)*; Story lists Campaign, Time Attack, Score Attack and Casual. Every item explains itself while selected.
- **Pick any episode or stage** you have reached, or a whole episode; Puzzle lists every solved puzzle and the next one.
- **Continue** picks up at the start of the episode you were playing, with the score and hearts you had then (a stage left part-way is not resumed, so quitting just before losing never pays).
- **Profiles**: several players on one PC, each with their own progress, records, settings and trophies.
- **Local**: two players, with the computer as player 2 for Versus.
- **Pause menu** in the same style, with Restart level or chapter and Quit to menu, back to BlockPlanet's menus.

## Ways to play

- **Time Attack**: Story's episodes as fast as you can, with LiveSplit-style splits: your time per stage and the difference against your best, green ahead and red behind.
- **Score Attack**: Story's stages against a clock: 2:00 for the thinnest spheres and 10 seconds more per layer; combos add time (less the longer a stage goes on); whole minutes carry to the next stage and the rest becomes points. Replaces the original's Time Trial and its separate stages, so every competitive mode uses Story's episodes.
- **Casual**: Story without a game over; no records or trophies.
- **Records** per stage and episode for each mode, saved in your profile.

## HUD

- **A new HUD for every mode** (Story and its modes, Hide & Seek, Puzzle, Lines, VS), in two tinted panels beside the sphere, placed as on a 4:3 screen and moved out of the sphere's way; rows above and below on tall screens. Graphics > HUD panels: near the sphere, at the screen's edges, or following the sphere.
- Story: hearts with the drop timer, the held magic turning in its box (and flying from it when used), the next pieces, the stage, a progress bar towards the core, and the time.
- Hide & Seek shows the level's goal in words and no progress bar (it would give the pictures away); Puzzle shows drops and slides left and your best moves; Lines shows its own; VS gives each player a column.
- The game's own cards (Lines' rules, Hide & Seek's goals) stay readable.

## Title screen

- Rebuilt for any screen size from the game's own 3D models: the 3D letters, a new 3D ring, Turbine and Wheels (coloured from their portraits and shaded), the Bomb magic's missiles, and the light from the exposed core.
- The copyright notice as sharp text, with a note that BlockPlanet is an unofficial fan project.

## Trophies

- Built-in trophies, including combos of 40 blocks, whole and final episodes in Time Attack and Score Attack, Overtime, and beating the developer's time or score on every stage.

## Look and text

- **BlockPlanet's font** for menus, HUD and much of the game's own text (the Try Again menu and others); more screens to follow.
- The sphere is drawn by BlockPlanet for sharper, smoother play at any resolution.

## Under the hood

- Performance work on frame pacing, audio timing and the renderer, measured before and after.
- The game's 3D scenes can use 8 MB of memory instead of the console's 4 MB.
- An asset catalogue tool that reads the game's pictures and 3D models from your own ROM (for development; nothing from the game is included).
- A session log in the data folder for bug reports.

## Getting started

Supply your own Tetrisphere US ROM when asked. BlockPlanet contains no game data: anything taken from the game is read from your ROM.

## Thanks

BlockPlanet builds on Blocksphere by djultra64, and on N64Recomp, N64ModernRuntime, RT64, RmlUi and the other projects listed in `THIRD_PARTY_NOTICES.md`.
