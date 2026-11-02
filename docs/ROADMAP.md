# BlockPlanet roadmap

A living plan. Each item lists what it involves, what it depends on, and how big it is. Sizes are rough: **S** is days, **M** is one to three weeks, **L** is one to two months, **XL** is a multi-month project. All sizes assume part-time work, testing on a real PC, and that the item's research does not turn up something unexpected.

## How the game is built, and why it matters for these plans

BlockPlanet runs the original game code, recompiled to PC code, inside N64ModernRuntime, with RT64 drawing the graphics. That shapes every item below:

- **Host-side changes** (menus, audio output, screen size, presentation, networking) are ordinary C++ and comparatively easy.
- **Game-side changes** (new magic, camera, HUD layout, sphere behavior) mean finding the relevant original functions and data, then hooking or replacing them. Blocksphere already built hooking tools for its widescreen HUD (`config/recomp/ui_anchor_callsites.json`, `tools/recomp/ui_anchor_hooks.py`). Each game-side item starts with a research step, and the research decides the real size.
- **Art** (HUD, menus, characters, visualizers) needs someone to make it. Texture replacement in RT64 means new art can be swapped in without touching game code.

## Phase 0: foundations (in progress)

These unblock most of what follows.

| Item | What it involves | Size |
| --- | --- | --- |
| Windows measurements | Run `--perf-log` on a real PC with each present mode and readback setting. Decides the performance work. | S |
| Deferred framebuffer readback | Copy each frame into game memory while the game builds the next one, instead of making it wait. See [frame pacing findings](research/blockplanet-frame-pacing.md). | M |
| Automated Windows builds | A script or private CI that turns a ROM into a Windows build without manual toolchain setup. Generated code stays private. | M |
| Modern UI layer | Integrate RecompFrontend / RmlUi (HTML/CSS-style layout, already pinned as a research dependency) as the host menu system. Most later UI items build on this. | L |

## Phase 1: quick wins

### Quality and performance modes (S, after Phase 0 measurements)

Presets in Options that combine resolution, MSAA, texture filtering, present mode, framebuffer readback and refresh rate. Performance favors a steady 60 FPS; Quality favors resolution and anti-aliasing. The measurements decide what each preset contains.

### Spheres not appearing completely or fast enough (research S, fix S to M)

Large multi-layer spheres seem to build in over several frames when they come into view. The likely cause is a per-frame work budget that made sense on N64 hardware. Plan: capture it happening, find the code that builds sphere geometry and any per-frame limit, then raise or remove the limit on PC. Blocksphere's 1.0 notes say it did not change sphere size or culling, so this area is untouched.

### Jukebox and sound test (M)

A menu that lists every music track and sound effect and plays them. Research: find the game's song table and the function that starts a song (the music format is documented by Chris Gilmore's `n64tetristools` and `dcm2xm` research, credited in the README). Then call that function from a new menu. Unlockables or track names can come later.

### Casual play: no game over (S, done as a launch option)

`--casual` refills hearts as soon as one is lost and stops the Time Trial clock at 30 seconds, so a game never ends. Trophies are paused while it is on. It becomes an Options setting with the new menus. Requested both for testing later stages and as a relaxed way to play.

## Phase 2: presentation

### Variable screen sizes: ultrawide, vertical and any window shape (L)

- **Wider than 16:9 (21:9, 32:9):** extend the existing widescreen work. RT64 already widens the projection, and Blocksphere's HUD anchors keep UI at the edges. The work is checking every scene for missing background edges, culling and framebuffer effects at extreme widths.
- **Taller than 4:3 (portrait or phone-like):** RT64 widens rather than heightens, so this needs a vertical field-of-view adjustment and new HUD anchoring for top and bottom edges.
- **Free resizing:** pick the projection from the actual window shape instead of fixed presets.

Depends on: research on the game's background layer, which is a fixed 320×240 image drawn as 40 tiles and will need extending or replacing at other aspect ratios. Do this together with the HUD redesign.

### Modern HUD and graphics (L, plus art)

Redesign the in-game HUD (score, hearts, magic, level and character portraits) with modern art that scales to any screen size. Two layers:
1. Replace the HUD textures through RT64 texture packs (art only, no code).
2. Change HUD layout and anchoring through the existing UI hooks so elements sit sensibly at every aspect ratio.

Depends on: the screen-size work, and an art direction decision.

### Modern menus and options screens (L, plus art)

Replace the original menus (title, mode select, options, controls, graphics) with new ones on the RmlUi layer. Game menus that change game state (mode select, name entry, save slots) must keep driving the original game logic, so each screen needs a map of which guest functions and memory it uses. Start with the host-only screens (graphics, controls, audio, jukebox), then replace game screens one by one.

**Secret codes become menu items.** The original unlocks extras by typing special names under NEW NAME: LINES (a hidden mode), CREDITS, G + alien icon + MEBOY (special music), and a five-icon sequence (planet, ship, rocket, heart, skull) for level select, as recorded in the 1997 guide cited in [modes-matrix.md](research/modes-matrix.md). The new menus offer these directly: Lines in the Single mode list, Credits as a menu entry, level select (below), and the special music in the jukebox. Research: find what each name sets in game state, so a menu item can do the same without the code.

**Level select by chapter.** Level select appears once a player has completed a chapter and offers every chapter that player has completed. Research: how the game records each player's progress in its save data (levels are numbered chapter:level, for example 1:1).

Direction: a modern, simple, TV-like menu (large focus states, few items per screen, quick transitions), with no option to keep the original menus.

## Phase 3: audio and visuals

### Background animations: understand them (research M)

Gameplay frames render an off-screen background layer at RDRAM `0x000400` with two projections, which the main frame then draws as forty 64×30 tiles. Research questions:
- Which code generates each background and its animation parameters (speed, color, geometry)?
- Are the backgrounds procedural, or stored animations?
- How much of each frame's GPU time do they cost?

### Music-reactive backgrounds, like a Winamp visualizer (M to L, after research)

Analyze the audio the game outputs (BlockPlanet already receives every PCM buffer in `queue_audio`) with an FFT to get bass, mid and treble levels and beat detection. Then either:
- **Drive the original backgrounds:** write those levels into the background's animation parameters each frame, if the research finds suitable ones. Original look, music-reactive.
- **Add new backgrounds:** draw host-side shader backgrounds into the background layer. Fully new visuals, the most freedom, and the basis for user-made visualizers later.

### Your own music: MP3, OGG and FLAC (M)

Play music files from the player's device instead of, or alongside, the game's soundtrack: silence the game's music channels, keep sound effects, and mix the decoded file into the output. Public-domain decoders such as `dr_mp3`, `dr_flac` and `stb_vorbis` fit the GPL license. Needs: a music folder setting, playlist and shuffle options, and the jukebox menu. Works well with the visualizer, which reacts to whatever is playing.

## Phase 4: gameplay

### Camera to peek at the back of the sphere (research M, then M to L)

The game only builds and draws the side of the sphere facing the camera. A peek camera means:
1. Finding the camera and visibility code.
2. Rotating the view on a button hold without changing game state.
3. Making the back blocks render, which may mean extending the visibility rules.

Needs care in VS and with game timing.

### Review magic (power weapons) and add new ones (research M, each new magic M to L)

First document every existing magic: trigger, effect code, animation, sound and how it is earned. New magic means adding entries to the game's magic tables and writing the effect logic as new C++ functions hooked into the game. Balance testing is needed for VS.

### Custom characters you can save (M to L, plus art)

Let players create characters (name, colors, portrait, look) and keep them in BlockPlanet's own save data rather than the 512-byte N64 EEPROM. Research: how the game stores and draws its characters. Then use texture replacement for the look, and hook the character selection to load custom entries.

### Trophies (M, engine done)

Built in, and open to launcher integration. See [TROPHIES.md](TROPHIES.md).

- **Done:** the data-driven trophy engine (events, stats, conditions, `all_of` and platinum), progress saved with a backup, guests earning nothing, a connector point for launchers, and a proposed list of 19 trophies. Lines is available from the start, so there is no trophy for finding it; Lines trophies can follow once its rules are known.
- **Game hooks, first set done:** games started, pieces dropped, hearts lost, game over and VS CPU rounds. The first trophies unlock in real play.
- **Next:** combos, level and chapter completion, magic and puzzles solved; a trophy screen and unlock notification in the new menus; and the owner's final trophy list.

### Quiver Launcher integration (S to M, after trophies and automated builds)

Goal: list BlockPlanet in [Quiver Launcher](https://github.com/tgeorgiadis/quiver-launcher), which installs, updates and runs PC ports and recompilations from GitHub and GitLab releases, and tie BlockPlanet's trophies into Quiver's achievement (trophy) system.

- **Trophies:** build BlockPlanet's trophies against a small reporting interface, with Quiver as the first implementation (Steam or others could follow). Quiver's public repository and release notes do not document a trophy or achievement API as of 2026-11-01. Version 4 was still in beta then, so this may be new or planned. Its website (quiverlauncher.com), where catalog and integration documentation probably lives, has not been reviewed yet.
- **Catalog:** Quiver installs from GitHub releases, so this needs the automated Windows builds and regular GitHub releases. Players still supply their own ROM.
- **Contact:** Quiver's developer opened Blocksphere's only issue, asking for its Windows build, so they already know the game.

## Phase 5: online multiplayer (XL)

Online play for up to 4 players, where each player's actions affect the other players' spheres.

- **The original game supports 2-player split-screen VS only.** Four spheres on one screen would be a large rewrite. The practical design is that **each player runs their own single-player game**, and BlockPlanet sends attacks between games over the network, using the attack mechanics VS already has where possible.
- **Networking:** a small relay or lobby server, room codes, and a message protocol for attacks, scores and player status. This is a more forgiving design than lockstep netplay, which would need fully deterministic emulation that the current runtime does not provide.
- **Research first:** how VS sends attacks between the two players, and whether that path can take attacks from a network message.
- **Other costs:** hosting the server, cheating, NAT traversal, and a lobby UI.

A sensible first milestone is two players online with VS-style attacks, then four.

**Host progress in online games.** Guests can play any stage the host has unlocked. Playing it does not count towards the guest's own completion, unlocks or trophies.

**Lines as a multiplayer mode.** Lines, the hidden mode, is a candidate for local and online VS. Research: how Lines' rules and win conditions differ from the modes VS already supports, and whether VS can run with Lines rules.

## Suggested order

1. Phase 0 measurements, then deferred readback.
2. Quality/performance modes, the sphere pop-in fix and the jukebox. Visible results early.
3. The modern UI layer, then menus, HUD and screen sizes together, since they share layout work.
4. Background research, the visualizer, then custom music.
5. Peek camera, magic and custom characters.
6. Online multiplayer, after its research spike.

## Decisions

- **Platform:** Windows first. The code already builds and runs on Linux, so Linux and Steam Deck releases can follow later as packaging and testing work.
- **HUD art:** made by the project owner. The current HUD stays until the new art is ready. [HUD_INVENTORY.md](HUD_INVENTORY.md) lists what the HUD shows, where and when.
- **Look:** modern only, with no "Classic" option. Menus should be simple and TV-like, with quick transitions.
- **Online:** private rooms joined with a friend code. No public matchmaking.

## Open questions

- Who hosts the online relay server?
- Should new magic and custom characters be allowed in online play, or only offline?
- Who makes the art for menus, characters and visualizers?
