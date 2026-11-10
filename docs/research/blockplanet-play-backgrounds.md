# Play backgrounds (research, started 2026-11-10)

The owner's question: the animated "Winamp style" backgrounds behind the sphere during play (the swirling purple and blue tunnel in Story 1:1, the red and orange one in Versus and Lines, and so on). How are they made? Do they follow scripts? Can we make our own, or change the existing ones?

Status: **open.** This page collects what is already known from earlier research and lists what is still to find, with the captures that would settle each point. Nothing taken from the game (textures, tables, code) goes in Git; this page gives addresses and descriptions only.

## Known so far

- **A tunnel, in 3D.** BlockPlanet's held-magic drawing (`src/graphics/magic_hooks.cpp`) had to move the magic nearer the camera so that it sits "in front of the tunnel, whose walls near the screen's edges are nearer than the bowl's spot". So at least Story's background is geometry with depth around the camera, not a flat full-screen picture. Whether every background is a tunnel is still to check.
- **Chosen per level, by a per-mode rule.** From the direct-launch session's catalogue (`docs/research/modes-matrix.md` on `direct-launch`):
  - The level set-up `func_800906E8` writes a background index into the level structure at `0x8013D264` next to the song (`0x8013D266`). The rule comes from a per-mode jump table at `0x800F0F98`.
  - The rules are:
    - Rescue, Lines and the demo: by chapter, from the table at `0x800E94C4`.
    - Time Trial: fixed, `0x800E94A4`.
    - Training: fixed, `0x800E949C`.
    - Puzzle: by group of ten puzzles, `0x800E9470`, indexed by the level counter ÷ 10.
    - Hide & Seek: by the record's group byte (`0x8013D248`), `0x800E9528`.
    - VS CPU: by round.
    - Mode 12: fixed, `0x800E94C0`.
  - `func_8006EE6C` (named "mode initialisation" in the early maps, `docs/research/m1-static-map.md`) reads the index as a lookup into two tables of asset ids (`0x800E2262`, `0x800E2296`) and loads them for the background. The current background is kept at `0x800E23BC`, and an index of 0 keeps the previous one.
  - So there is a small fixed set of backgrounds, each named by two asset ids: probably a model or scene and its texture or animation. The level files (`docs/research/blockplanet-level-data.md`) also copy bytes 15 and 16 to `0x8013D260`, `0x8013D264` and `0x8013D25C`, so a level record can also name its background.
- **Assets come from the ROM archive.** The asset ids are the same kind the game loads elsewhere from its main archive, which BlockPlanet already reads for the menu grid's marble (`src/ui/grid_texture.cpp`, `unpack_archive_entry`). The background ids would be readable the same way at run time.

## Still to find

1. **How it is drawn.**
   - Is it a mesh with scrolling textures (texture coordinates changed each frame, or a tile offset set in the display list)? Is it a colour-cycled palette (a TLUT rewritten each frame)? Are its vertex colours animated? Or is it a full-screen pass?
   - Which function draws it, and where in the frame: it should come before the sphere, the board and the HUD.
   - What its textures are: sizes, formats (CI4/CI8 with a palette would suggest palette cycling) and archive entries.
2. **What drives the animation.**
   - Fixed code per frame, or data per background: speeds, colours, palettes, keyframes, or perhaps a LightWave scene like the 3D models (`func_800321D4`, the LightWave player's per-object draw)?
   - Does the music drive it? `docs/REDESIGN.md` calls the backgrounds "music-driven". That is unconfirmed.
3. **How many backgrounds there are:** the length of the tables at `0x800E2262` and `0x800E2296`.
4. **Confirm the choice in play.** Watch `0x8013D264` and `0x800E23BC` across a level change in Story (chapter 1 to 2), Puzzle (level 10 to 11) and VS (round 1 to 2), and check that the background changes when they do.

## Captures and reads wanted (real game)

Each one names the switch to use.

- **A. The asset ids and the table sizes:** read 32 entries at `0x800E2262` and `0x800E2296`, and note where they stop looking like ids.
- **B. Watches:**
  - Story 1:1 to 1:2 to 2:1, VS round 1 to 2, Puzzle 10 to 11.
  - Use `BLOCKPLANET_WATCH="0x8013D264:2,0x800E23BC:2,0x8013D260:2,0x8013D25C:2"`.
  - Save a frame of each background.
- **C. Is it a model?** Run `BLOCKPLANET_MODEL_LOG=1` and `BLOCKPLANET_OBJECT_LOG=1` in Story 1:1 and Lines. If the background goes through `func_8002D93C` or the LightWave player, `BLOCKPLANET_MODEL_SKIP` or `BLOCKPLANET_OBJECT_ONLY` can hide it or show it alone, which settles what is background.
- **D. Is it 2D?** Run `BLOCKPLANET_DRAW_LOG=1` during play, for any full-screen draw.
- **E. The display list:** take one frame of Story 1:1 in the RT64 inspector. Record the first draw calls (before the sphere):
  - vertex count, texture size and format;
  - texture-coordinate offsets or tile `uls`/`ult` values, compared across two frames a second apart;
  - whether a TLUT load appears and changes between the frames.
- **F. Code:** in the generated code, read what `func_8006EE6C` does with the two ids, and who reads `0x800E23BC` during play (a gdb read watch). That function is the background's per-frame driver.

## Feasibility (provisional, to revise once A to F are in)

| Option | What it takes | Effort | Risk |
| --- | --- | --- | --- |
| (a) Choose a different existing background per level | Write `0x8013D264` (or the per-mode tables) at level set-up, as BlockPlanet already writes other level values | Small | Low: the game's own path, with assets it already loads |
| (a) Change an existing one's colours, speed or texture | Depends on F. Data in RAM (a speed, a palette) can be overwritten after load. Values hard-coded in the per-frame driver need a hook on it, as for the magic and the pause | Small to medium | Medium: needs the driver understood; speeds must stay tied to the game's frame, not the display |
| (b) New ones through the game's renderer | New parameters, palettes or textures fed to the same driver, with textures replaced at load (RT64 texture packs, or our own image in place of an archive entry) | Medium | Medium: possible only if the driver is data-driven; otherwise each new style is new code |
| (b) BlockPlanet draws its own background | Skip the game's background draw (as the HUD and pause draws are skipped) and draw a shader or mesh of our own behind the sphere, like the menu grid but below the game's 3D | Medium to large | Medium: it needs a hook point between the background and the sphere in RT64's frame, and depth must stay right for the magic and the sphere |

A note on ownership: backgrounds BlockPlanet draws itself (the last row) would be entirely BlockPlanet's own art and code, so they could be committed. Changes to the original ones must keep using the player's ROM at run time.
