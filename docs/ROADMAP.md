# BlockPlanet roadmap

A living plan. Each item lists what it involves, what it depends on, and how big it is. Sizes are rough: **S** is days, **M** is one to three weeks, **L** is one to two months, **XL** is a multi-month project. All sizes assume part-time work, testing on a real PC, and that the item's research does not turn up something unexpected.

The longer-term design direction (visual identity, camera, story, modes, junk rules, media player, accessibility) is in [REDESIGN.md](REDESIGN.md). This roadmap covers what to build and in what order.

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

### Quality and performance modes (M, after Phase 0 measurements)

Presets in Options that turn several dials together, so the game scales from handhelds to high-end PCs. Each dial is measured with `--perf-log` on real hardware before defaults are chosen.

| Dial | Performance | Quality |
| --- | --- | --- |
| Internal resolution | native or 2× | 4K class |
| MSAA | off | 4× or 8× |
| Sphere detail (see below) | Original or Extended | Extended or Full |
| Framebuffer readback | deferred | full |
| Frame rate target | 60, or 30 on weak handhelds | 60, 120 and above |
| Background and visualizer effects | simple | full |
| Texture filtering | basic | enhanced |

**Sphere detail** has three steps, because the game draws a fixed patch of the sphere around the view (see [blockplanet-game-state.md](research/blockplanet-game-state.md)):
1. **Original:** the N64 patch. Cheapest, but blocks go missing near the edge of the view.
2. **Extended:** covers the whole visible half of the sphere and fixes the edge problems. Available as the SPHERE graphics setting (two extra cells on every side), and the default.
3. **Full:** every block with an exposed side, which the rear or peek camera needs. This step also makes the far side visible through an opened core: the undersides of far-side blocks, darkened so they read as distant and in shadow. The original never draws them.

The larger steps cost mostly CPU (the game builds the vertices itself) and buffer memory rather than GPU time, so they may matter more on handhelds than on desktop PCs. Where Full is too expensive, the rear camera can fall back to a simpler view rather than being removed.

**Handhelds:** Steam Deck and Windows handhelds run the PC build, so they need tuned presets rather than a port. Android would need the recompiled C built for ARM and the graphics and platform layers ported.

### Spheres not appearing completely or fast enough (research S, fix S to M)

Large multi-layer spheres seem to build in over several frames when they come into view. The likely cause is a per-frame work budget that made sense on N64 hardware. Plan: capture it happening, find the code that builds sphere geometry and any per-frame limit, then raise or remove the limit on PC. Blocksphere's 1.0 notes say it did not change sphere size or culling, so this area is untouched.

### Jukebox and sound test (M)

A menu that lists every music track and sound effect and plays them. Research: find the game's song table and the function that starts a song (the music format is documented by Chris Gilmore's `n64tetristools` and `dcm2xm` research, credited in the README). Then call that function from a new menu. Unlockables or track names can come later.

### Casual play: no game over (S, done as a launch option)

`--casual` refills hearts as soon as one is lost and stops the Time Trial clock at 30 seconds, so a game never ends. Trophies are paused while it is on. It becomes an Options setting with the new menus. Requested both for testing later stages and as a relaxed way to play.

## Phase 2: presentation

### Variable screen sizes: ultrawide, vertical and any window shape (L)

- **Wider than 16:9 (21:9, 32:9): done as the Picture setting's Fill window** (now the default): RT64's expand mode follows the window's shape, and Blocksphere's HUD anchors keep the HUD at the edges. Checked at 21:9, 32:9 and 16:10 on the title, in play and in the pause; other scenes still to be looked at on a real ultrawide screen.
- **Taller than 4:3 (portrait or phone-like):** RT64 widens rather than heightens, so this needs a vertical field-of-view adjustment and new HUD anchoring for top and bottom edges. The sphere then moves closer (camera distance, player context `+0x2420`, × 4:3 ÷ the window's shape) so it keeps the original's share of the narrow side.
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

Idea (2026-11-05): a **scanner** magic that, when used, briefly highlights the board's weakest point: the core-sized window with the fewest blocks still above the core (the same calculation as Story's progress meter). It is earned like other magic, or found on the sphere as a special block, so the base game does not get easier; the hint is a reward.

Idea for later (2026-11-05): a **second magic slot** (hold two items, so a new one does not replace the one held), perhaps as a casual-mode option. It changes the rules (which item fires first, VS balance), so it is not part of the HUD work.

### Custom levels (research M, then L)

Levels are generated: the game fills a 32×32 grid per layer with random pieces around a fixed core, and the chapter sets the depth (see [blockplanet-game-state.md](research/blockplanet-game-state.md)). A level editor could write a designed grid before play starts. Research: what each cell value means (piece, colour, crystal, link to neighbours), and where the generator runs, so a custom grid can replace its output cleanly.

### To research later (2026-11-06)

- **The attract-mode demos:** left idle on the title, the game plays demo levels. Are they scripted (a stored sphere and a recorded list of moves and pieces, replayed) or played live by the game's own CPU player (as in VS CPU)? If recorded, the recordings could be replaced (our own demos, or new modes shown off); if the CPU plays, it could demo any mode. Related: the seeded stages idea below.
- **The credits:** how the credits sequence is scripted (the text is in memory as a list of names and roles, around `0x800EBF00`; see [research](research/blockplanet-game-state.md#the-title-screens-artwork-2026-11-06)), what drives the order, timing and layout, and whether it can be changed (adding BlockPlanet's credits after the original's, as the fork's own).
- **The music (BGM):** what the 23 tracks are (names in [audio research](research/blockplanet-game-state.md#audio-settings-2026-11-05)), what format the game uses (sequences played on its own instruments, like MIDI, or recorded audio), and how to edit them or write new ones with the game's own instruments ("the soundscape"), as an alternative or complement to playing the player's own music files (below).

### Time Attack and Score Attack on Story's episodes (built 2026-11-06)

Both modes play Story's episodes, whole or one stage at a time, from Solo: Time Attack as fast as possible (LiveSplit-style splits), Score Attack for the most points (the same splits by points). The original's Time Trial and its stages are no longer offered. Still to decide: whether they wait for seeded stages so times and scores are fair, and whether they keep Story's lives and game over (they do for now).

### Furthest stage reached as a record (idea, 2026-11-06)

Not for Time Attack (its players are speedrunning, having finished the game), but a good record for the **Multiphasic** mode: the furthest stage reached in a run, shown on its HUD and in the records.

### Seeded stages for time trials (idea, saved for later, 2026-11-06)

A **seed number** for each level, so the same stage can be replayed exactly and timed fairly, level by level as well as by episode: shareable seeds, a "daily stage", personal and (later) online bests per seed. Today every level's board is generated randomly at each start ([level data](research/blockplanet-level-data.md); two starts of 1:1 differed in 887 of 1,024 cells), and the piece queue is random too ([piece queue](research/blockplanet-game-state.md#the-piece-queue-2026-11-05)), so stage times are only comparable per episode. To find out: where the game's random number generator lives and how it is seeded; whether seeding it at a level's start reproduces both the board and the piece sequence (the queue keeps eight pieces ahead, so the sequence must come from the same seeded source); what else draws from it during play (effects, magic, the CPU in VS), which could make play diverge even with the same seed; and how a seed is shown and entered (the level select, the records file). Puzzle's boards are already fixed, so its bests are fair as they are.

### New block types (idea, saved for later, 2026-11-05)

Investigate adding block types of our own. What is known: a board cell holds value = type × 9 + a variant (`func_8008A7EC` reads the type), so there is room for new type numbers; types seen so far are 1 (unknown), 7 (special, probably buried bonuses), 10 (the core), 32–37 (the colours), 38 (the "?" piece) and 47 (Puzzle's inert wall). Writing a type straight into the board already works (the type 47 test, the one-colour test). To find out: which code decides how each type looks (texture and model), how it matches and clears, how gravity and magic treat it, and whether the per-type checks (for example the 8 to 30 range in `func_8008A880`) leave free numbers. Then a new type is new art plus a behaviour hooked into those checks.

### Arena walls for new game types (idea, saved for later)

Puzzle limits its play area with a border of type 47 blocks (the purple "out of bounds"). Tested in Rescue, they act as an inert wall: drawn purple, never cleared, pieces land on them ([research](research/blockplanet-game-state.md#puzzle-boards-the-purple-border-2026-11-05)). That makes arenas of any shape, obstacles and shrinking arenas possible without changing game code; place them as whole cells when a level is built. Still to test when a mode needs them: dragging onto or across them, and magic.

### New game modes (ideas, saved for later, 2026-11-05)

**Escape.** The player and the camera start inside the sphere. No core layer is visible. The player drops blocks onto the underside of the existing blocks. Orphaned blocks can still drop to the core with gravity. Blocks that miss their target cannot stay under the core line: if the player misses, the block dissolves and they lose a life.

**Multiphasic.** Several levels stitched together seamlessly through visual trickery. It starts with stage 1, which looks the same as usual: dig down to the core. But the core looks like a different sphere with a different block combination (purely representative, the core picture design) and is faded. The core cannot be destroyed, and anything dropped on top of it follows the normal core rules. Once the target core size is reached (4 × 4 etc.), there is a detonation and all remaining blocks are thrown off the sphere. The trick (the transition): what remains is the core, decorated to look exactly like the next stage. It grows to match the next phase's (level's) starting size and is replaced by the actual next level, and play continues with the new block set. As it is a long game, it needs a way to earn lives.

**Lines variants (later).** Lines could also get Time Attack, Score Attack, VS and co-op (Cooperative) versions.

**Endless Score Attack** (was "Score Attack" before the game's Time Trial took that name, 2026-11-06). Score is the whole goal: the core cannot break and the sphere regenerates, so play goes on and only the points count. The score is shown prominently (in Story it is not; see the HUD notes). Multiphasic, as a long game, would also show a score.

First technical notes (not yet researched):

- **Escape:** the camera inside the sphere means the renderer drawing the inner faces and culling differently (BlockPlanet now draws the sphere itself, `docs/research/blockplanet-game-state.md`). Dropping onto the underside reverses the drop direction and gravity, which is game logic spread over many functions (the board limits table in the same document); likely the largest change of the two.
- **Multiphasic:** the transition fits what is already known: the core can be resized in the mesh builder (the `BLOCKPLANET_SPHERE_CORE_SCALE` experiment), BlockPlanet draws the sphere, and an indestructible block type exists (type 47, Puzzle's border). The "core picture" stage would need art for the next stage drawn on the core, and a way to load the next level without leaving play (the game builds a level between levels today). Earning lives needs a rule (for example a score or combo threshold) and a HUD element.

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

**Cooperative** (two players, one sphere, drop and support roles) is specified in [REDESIGN.md](REDESIGN.md#8-cooperative-new). Local Cooperative can come before online play; it depends on the sphere mapping from the culling research and on the magic research.

Online play for up to 4 players, where each player's actions affect the other players' spheres.

- **The original game supports 2-player split-screen VS only.** Four spheres on one screen would be a large rewrite. The practical design is that **each player runs their own single-player game**, and BlockPlanet sends attacks between games over the network, using the attack mechanics VS already has where possible.
- **Networking:** a small relay or lobby server, room codes, and a message protocol for attacks, scores and player status. This is a more forgiving design than lockstep netplay, which would need fully deterministic emulation that the current runtime does not provide.
- **Research first:** how VS sends attacks between the two players, and whether that path can take attacks from a network message.
- **Other costs:** hosting the server, cheating, NAT traversal, and a lobby UI.

A sensible first milestone is two players online with VS-style attacks, then four.

**Host progress in online games.** Guests can play any stage the host has unlocked. Playing it does not count towards the guest's own completion, unlocks or trophies.

**Lines as a multiplayer mode.** Lines, the hidden mode, is a candidate for local and online VS. Research: how Lines' rules and win conditions differ from the modes VS already supports, and whether VS can run with Lines rules.

**VS on Story's episodes (idea, saved for later, 2026-11-06).** VS CPU has its own ladder (5 episodes of 7 rounds, endings after 3 and 5). The idea: play Story's episodes in VS instead (whole episode or one stage against the computer), so stages are the same in every mode. To research first: which level records VS loads, where it counts rounds per episode (overriding `func_80089C9C` did not change Rescue's count), whether its start uses Story's next-level counter (`0x800E449C`), how the computer's skill follows the episode, and VS's core-size addition. Kept as the original for now.

**Join codes for streamers (idea, 2026-11-06).** Friend and lobby codes shown hidden (`••••••`) with a reveal button and a **Copy** button that works while hidden; a **streamer mode** setting hides codes everywhere by default (lobby, notifications) and perhaps friends' names. See REDESIGN.md, section 4.

**Credits (reminder, 2026-11-07).** When the credits are done, add a section for the repositories BlockPlanet builds on or referenced (Blocksphere, N64Recomp, N64ModernRuntime, RT64, RmlUi and others in THIRD_PARTY_NOTICES.md), and the owner's Git username.

**Animated title characters (nice to have, idea 2026-11-07).** Turbine and Wheels are held on one frame of their scenes; letting them move (an idle loop: tentacles swaying, a wheel turning, the Bombs drifting) would bring the title to life. The scene code already animates them; what is needed is a short loop that stays inside the ring, and Wheels' own scene.

**Real lighting and shadows (nice to have, once everything else is done; idea 2026-11-07).** The game's lights are N64 lights: a few directional lights and an ambient level, worked out per vertex, so sides facing a light brighten but nothing casts shadows. Steps, by effort: (1) light BlockPlanet's own geometry properly (the title ring and rebuilt title parts) and use the game's lights on its models, done with the title work; (2) per-pixel lighting for crisp highlights (RT64 or a separate modern pass); (3) real shadows, such as the letters on the ring or blocks on the inside of the sphere, which would make the Escape mode's view from inside readable (a light at the core, blocks shading the inner surface). Step 3 likely means extending RT64; its code needs a look before any promise. BlockPlanet builds the sphere's geometry itself, so the sphere is the easiest place to experiment.

## Single player first

Decided 2026-11-02. Every mode runs on the same engine (sphere, board, pieces, magic, scoring, camera, HUD, renderer, audio, controls, saves); the modes differ mainly in their rules. So the modernisation is finished once, through single player, with Rescue (Story) as the reference mode because it uses the most systems. The other solo modes, then multiplayer, are built on the result rather than updated one by one from their originals.

**Single player is finished when:**

1. Performance is measured on Windows, with quality and performance presets (the extended sphere is done).
2. Camera and screen sizes work, including ultrawide, as the base for the rear and peek camera. (Ultrawide done. The game has no sphere camera; the sphere's size on screen is set through its mesh, now adjustable: see the research notes.)
3. The new menu layer exists, with the main menu, chapter select and profile screen. (Profiles done: choose, add, name, character, delete and guest.)
4. The HUD has the new art and a layout that adapts to the screen.
5. The Story structure is in place: chapter map, unlocks, casual play ([STORY_PROGRESSION.md](STORY_PROGRESSION.md) is the stage-by-stage table to plan rewards in). Research done on the original's structure (10 episodes, the first ending with credits after 5, harder episodes 6–8 and a final victory, bonus episodes 9–10, a message scene after each other chapter): see the research notes.
6. The see-through core and the rear and peek camera work.
7. Options and accessibility are complete.
8. The other solo modes (Time Trial, Puzzle, Lines, Custom) are built on the result.

**Training is Chapter 0** (decided 2026-11-05): Training is not a menu item. Story's Chapter 0 is the game's Training Basics, shown in the chapter list and skippable (done). Modes that share Story's stages share its Chapter 0 (Hide & Seek uses the same chapters; Time Trial and Lines to be checked). Other level sets get their own Chapter 0: the game's Training Puzzle is Puzzle's Chapter 0 (done), and Training Advanced (crystal pieces, X count, fuse combos) could become Chapter 0-2 later. New lessons (more Chapter 0 parts, or ones for other modes) need writing and design.

**Multiplayer checklist,** reviewed whenever a shared piece is finished, so nothing is designed into a corner:

- Two spheres at once (Versus): the extended sphere and the performance budget must cover both; the extended patch currently switches itself off with two players.
- Split-screen camera, aspect ratios and HUD space for two to four players.
- Controls and profiles per player (designed for this already).
- One sphere with several cursors (Cooperative): outlines, highlights, the see-through core and the peek camera must allow more than one player.

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
- **Saves:** kept in one folder per PC, shared by every Windows account (`C:\ProgramData\BlockPlanet`), with a portable option; several player profiles, each with its own save, trophies and progress, plus guest play. A save file per profile holds progress and preferences; records (best times, scores) are a separate file shared by the PC. Controls belong to the profile. Profiles are chosen, created or deleted at launch, and per player in local multiplayer. See [SAVES.md](SAVES.md).
- **Order:** get the modes working as designed (see [REDESIGN.md](REDESIGN.md)) before extending trophies and statistics; the events they need (combos, blocks destroyed, magic, junk, battle results) come with that work.
- **Profile characters:** each profile picks a character to represent it, from the characters rescued in the game (with optional colour variants), with more added later.
- **Board:** the board stays 32 × 32 cells with 9 layers, as in the game. Its limits are built into about 70 game functions (see [blockplanet-game-state.md](research/blockplanet-game-state.md#layers-core-size-and-stacking-2026-11-02)). Sphere changes are visual only: drawing more of it, and possibly a different core size.
- **Story game over:** kept as the original: unlimited Try Agains, each restarting the chapter from its first level with full hearts and the score reset (tested: 11 retries in a row, never refused). Every mode with chapters whose game over goes back to the chapter's first level (Story, Hide & Seek) offers Restart chapter in its pause menu, after a confirmation, rather than Restart level; other modes offer Restart level: getting through a chapter without a game over is the challenge.
- **Sphere framing:** as close to the original as possible. The sphere keeps the original's size relative to the screen's narrow side at rest, its growth with stage depth (about 55% of the height at 1:1, larger than the screen at 10:1) and the drop-timer zoom (up to about twice the size). The camera rests at the same distance at every stage (confirmed at 1:1 and 10:1), so deep stages may overflow the screen at rest, as in the original. With Fill window this already holds for every screen 4:3 or wider; portrait and squarish windows will get it with the portrait work, by moving the camera (player context `+0x2420`) closer by 4:3 ÷ the window's shape. No Sphere size setting.

## Open questions

- Who hosts the online relay server?
- Should new magic and custom characters be allowed in online play, or only offline?
- Who makes the art for menus, characters and visualizers?
- Core size: keep the game's, or make it bigger? If bigger, which stays fixed: the top of the tallest possible stack (thinner layers), each level's surface, or the layer thickness (a bigger sphere)? `BLOCKPLANET_SPHERE_CORE_SCALE` is the experiment.
