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
3. **VORTEX.** The VORTEX name code plays an animation (docs/research/fan-feedback.md); finding what it starts could give a ready-made visualiser from the game itself. Research first.
4. **Pictures of models.** Not possible without drawing the models ourselves (BlockPlanet keeps nothing from the ROM in the repository; anything taken from it is read at run time).

Suggested next steps: try 3 (cheap research), then 1 or 2 as a "Planet" background.
