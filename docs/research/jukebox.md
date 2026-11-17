# The jukebox: what it has, what is missing, and the game's models (2026-11-17)

The owner's request (2026-11-17): "research missing elements and why some music can't be played", and "a few winamp inspired visualisations. If there's a way to incorporate models from the game that might be nice". The tiles stay the default background.

## Why tracks 15 to 23 did not play (fixed)

They are the game's bonus music. Its song routine (`func_8007F720`) refuses any track above a count at `0x800E28E4`, which is 13 until the G'MEBOY name code raises it to 22. The jukebox now raises the count just for its own call, and while a pause-menu choice of a bonus track lasts, so the game's Auto and Random music is unchanged. Details: docs/research/blockplanet-game-state.md, "The hidden music".

**The owner's note (2026-11-17):** the secret music might need to be added to the unlockables (docs/ROADMAP.md, Jukebox). Still open: should the nine bonus tracks be marked "Bonus" in the playlist, or locked until something is done (as the name code did), or simply open as now?

## Winamp's parts, and the jukebox's

| Winamp | Jukebox | Notes |
|---|---|---|
| Play, Stop, Previous, Next | Built | Previous restarts the track after its first few seconds, as Winamp does |
| Pause | Not yet | Stop silences the music; the song runs on underneath. A true pause needs the game's song player held still (its sequence player at `0x80107290`; candidates: stop its tick, or set its tempo to nothing). Research. |
| Time played | Built | Unknown ("-:--") for the menus' own music until a track is chosen |
| Time left, seek bar | Not yet | Needs each song's length: the songs are sequences that loop, so the length is the position of the loop point. Chris Gilmore's song-format research (`dcm2xm`, credited in the README) is the place to start. With lengths, Repeat off could move on at the real end instead of after three minutes. |
| Shuffle, Repeat | Built | |
| Playlist | Built | All 23 tracks |
| Volume | Built | The saved music level |
| Balance | Not yet | Easy on the PC side (scale left and right as the sound is queued) |
| Equaliser (10 bands) | Not yet | Possible on the PC side: filters on the sound before it is queued, as the spectrum already reads it. Medium. |
| Crossfade | Not yet | The game's own song fade (`func_80079B80` fades) could be used between tracks |
| Spectrum and oscilloscope | Built | The small spectrum in the display; Spectrum and Scope full screen |
| Visualisers (AVS, MilkDrop) | Started | Pulse and Bars on the tiles; Spectrum, Scope and Pieces full screen, built from the game's own brick texture |
| Hide the player (to watch) | Built | The eye button; any button brings it back |
| Skins | Not yet | With the themes on the roadmap |
| Song information (bitrate, kHz) | Not yet | Could show the output rate and "Sequenced"; little value |
| Media keys (Play/Pause, Next on a keyboard) | Not yet | SDL sees them; small |
| Remembered settings | Not yet | Shuffle, Repeat and the background reset each session; to keep in the profile |
| Sound test (effects) | Not yet | The roadmap's other half of this item |
| Own music | Not yet | Roadmap: play the player's files instead of the game's songs |

## The game's models as a background

The visualisers drawn today are BlockPlanet's own elements over the game, coloured from the game's grey marble brick (the menu grid's texture, read from the ROM at run time). The game's 3D models are drawn by the game itself through RT64, so they can only appear where the game draws them. Ways in, from easiest:

1. **The game's own screen behind the player.** With no grid, the menus show the game's picture underneath. A background that is the game's title scene (the big sphere and the logo) would need the game sent to that scene while the jukebox is open, then back. Medium; risky only in leaving the game in the right state.
2. **The intro's sphere, moved by the music.** BlockPlanet already moves the intro scene's bricks (src/graphics/intro_logo_hooks.cpp rewrites their matrices). Played as the jukebox's background, the bricks could pulse and the sphere spin with the music. Large, but the most "made from the game" option; needs the intro scene kept running instead of ending.
3. **VORTEX** (found 2026-11-17, below): a ready-made animation of the game's own models, with its own song.
4. **Pictures of models.** Not possible without drawing the models ourselves (BlockPlanet keeps nothing from the ROM in the repository; anything taken from it is read at run time).

Done: VORTEX as a jukebox background (below). Next: 1 or 2 as a "Planet" background.

## VORTEX (found 2026-11-17)

**What it is in the original.** Typing VORTEX at New Name sets `0x800E12F4` = 1 (`func_8004127C`, at `0x80044BB0`; not saved, so for that session only) and plays a confirming sound. It changes **the screen shown while the console's Reset button is held.** When Reset is pressed, the scheduler sends the game a pre-reset message (type 4 in `func_8002A140`, which sets `0x800DF728`). The top-level loop then runs `func_8005C410` every frame (`0x800E11F8` = 1) in place of everything else. Normally that is a fade to black that waits for the console to restart. With VORTEX on, once Reset has been held for 300 frames (five seconds; it busy-waits on the frame counter `0x800E120C`), it:

- builds a LightWave scene, **scene `0x130` with nine objects** (`0x7B`, 0, 1, `0x31`, 3, 4, 5, 6, `0x22`), into the model pool, pointer at `0x800E1264` (`func_8002CF70`, the same builder as the title and play backgrounds);
- starts **song `0x143`** (323) on the jingle player (`0x80109DC0`), at full volume;
- draws the scene each frame (`func_8002D93C`) and loops it when it ends.

**What it looks like** (seen in BlockPlanet, 2026-11-17): a glowing orange and green ring with a teal spiral turning inside it, and the game's robot characters (the round yellow robot, the green one, the long creature and others) swept past the camera and sucked into it one after another, matching the cheat sites' description (docs/research/cheat-codes.md). It sits right of centre on a 16:9 screen; the title's grid sphere stays behind it.

**Why it could not be seen in BlockPlanet before.** The five-second wait is a busy loop on a counter the retrace handler (another game thread) advances. The runtime runs one game thread at a time, so the loop never ended and the game froze. Fixed with a hook in the loop (`blockplanet_reset_wait`, at `0x8005C688`) that lets the other threads run, as BlockPlanet already does for the game's other retrace wait. BlockPlanet has no Reset button, so this matters only for research for now.

**Seeing it:** the research command `research_vortex` (test harness: `poke cmd:research_vortex`) sets the flag and starts the reset screen as if Reset were held; the vortex appears about five seconds later. There is no way back from it yet (on the console, letting go of Reset restarts the game).

**For the jukebox:**
- **The Vortex background (built 2026-11-17, `src/graphics/vortex_backdrop.cpp`).** Chosen in the jukebox, the game's reset screen is started behind it with the flag on, and undone when another background is chosen or the jukebox is left. What the reset screen changes: it rewinds the model pool to the mark after the common assets (`*0x801026B8`; the title's and menus' models live above it), copies the last picture into its fade buffer (`*0x800DFF00`), sets its fade state (`0x800DFF08` = 4) and its flags, and stops and restarts the jingle player. So, at the top of the game's frame (the top-level loop, `0x80070180`): entering saves the pool from the mark to its top (about 600 KB on the title) and the values it changes (`0x800DFEF0`–`0x800DFF20`, `0x800E11F0`–`0x800E1270`, `0x800E12A0`–`0x800E12D8`, the flag, `0x8010B330`–`0x8010B360`, the pool top), then sets the flag, `0x800E11F8` = 1 and the counters to 0; the counter is then set to 298, so the scene comes at once but steps a frame at a time; the copied picture is filled with an opaque dark colour (16-bit with the alpha bit: zero, transparent, hid the scene too); the vortex's song is stopped as soon as it starts, so the jukebox's track plays on. Leaving puts it all back. While it shows, BlockPlanet's hold on the idle counters and its hiding of the game's menu scenes are off.
  - **The crash found on the way:** waiting for the graphics tasks in flight (calling the game's `func_8002A140`, as it does before rebuilding models) on entering crashed the renderer about every other time: it takes the scheduler's messages out of the game's turn. Without it: 13 runs, 27 entries and leavings, no crash; play (a Training lesson) starts normally afterwards.
- **Still to do:** the scene sits right of centre on a wide screen (its camera is the console's 4:3 one).
- **Three more songs**: the game has songs outside the 23 tracks, started on the jingle player: `0x129` (the intro), `0x134` (the title) and `0x143` (the vortex). They could join the jukebox as extra tracks (and the secret ones among the unlockables).
