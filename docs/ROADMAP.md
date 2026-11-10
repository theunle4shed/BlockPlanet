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

**Found so far (2026-11-10, [research](research/blockplanet-play-backgrounds.md)):** each is a stored animation: a LightWave scene with one object, keyframed, played and looped in real time by the game's general model player. There are 25, picked per level by the mode's rule. They are not linked to the music. Only the mesh moves: one small textured mesh (one 32 × 32 texture), turned by its keyframes and shaded by the game's lights, which give the colour shifts. No textures scroll and no palettes cycle (`BLOCKPLANET_BG_DL_LOG`). Still open: the GPU cost.

### Music-reactive backgrounds, like a Winamp visualizer (M to L, after research)

Analyze the audio the game outputs (BlockPlanet already receives every PCM buffer in `queue_audio`) with an FFT to get bass, mid and treble levels and beat detection. Then either:
- **Drive the original backgrounds:** write those levels into the background's animation parameters each frame, if the research finds suitable ones. Original look, music-reactive.
- **Add new backgrounds:** draw host-side shader backgrounds into the background layer. Fully new visuals, the most freedom, and the basis for user-made visualizers later.

### Pinch mode: a background that reacts near game over (idea, research first, 2026-11-10)

The owner's idea: when the player is close to losing, the background behaves differently or reacts.
- **Triggers to consider:**
  - the last heart (player context `0x80110220` + `0x2D7E`);
  - the drop timer or shot time limit nearly run out (candidates `+0x3254` and `+0x3250`, from the direct-launch session's catalogue);
  - the board close to its limit;
  - any danger signal the original already gives (a music change, warning sound or HUD flash near game over), still to check.
- **Reactions to consider:**
  - a faster or slower background;
  - a tint or pulse;
  - a switch to another of the 25 backgrounds;
  - a BlockPlanet overlay effect.

See [the feasibility notes](research/blockplanet-play-backgrounds.md#pinch-mode-the-owners-idea-2026-11-10). Nothing is built until the owner decides.

### Counter-motion backgrounds (idea, research first, 2026-11-10)

The owner's idea: the background turns against the sphere's spin, or with it. Today it does not react at all: measured in Story 1:1, it changes no more while the sphere spins than when idle. The background would follow the sphere's rotation (the board's view position plus the sub-cell rotation used by `func_800ADD6C`, addresses still to name), applied as a rotation of the background's matrices at its draw (`0x80071BA0`), extra animation speed, or a shift of the background layer. See [the notes](research/blockplanet-play-backgrounds.md#counter-motion-the-owners-second-idea-2026-11-10). Nothing is built until the owner decides.

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

Investigate adding block types of our own. What is known: a board cell holds value = type × 9 + a variant (`func_8008A7EC` reads the type), so there is room for new type numbers; types seen so far are 1 (unknown), 7 (special: probably the mystery block, the shimmering cell that gives magic), 10 (the core), 32–37 (the colours), 38 (the "?" Random Block) and 47 (Puzzle's inert wall). Writing a type straight into the board already works (the type 47 test, the one-colour test). To find out: which code decides how each type looks (texture and model), how it matches and clears, how gravity and magic treat it, and whether the per-type checks (for example the 8 to 30 range in `func_8008A880`) leave free numbers. Then a new type is new art plus a behaviour hooked into those checks.

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

**Every screen in BlockPlanet's style (decided 2026-11-07).** For consistency, every screen the player sees should use BlockPlanet's design and font (Lato), not the original's pixel lettering. Already done: the menus, pause, the HUD in every mode, the title notice. Still the original's, to replace one by one (hide the original's text and draw BlockPlanet's, as the pause screen does), roughly in order of how often they are seen:
1. The scores and game-over screen, Try Again, and the end-of-episode Continue / Exit menu.
2. The episode cards ("EPISODE 2") and the messages between episodes.
3. Hide & Seek's goal card and Lines' rules card (shown now through the original HUD, `0x801033AC`).
4. Training's instructor text and pages (tied to the lessons' timing).
5. The credits, the endings' text and name entry.
6. Leftover original HUD pieces (the multiplier badge where it still shows).
Each needs its text found (much of it is pictures in the main archive; the asset catalogue lists them) and the screen's timing kept.

**Progress (2026-11-07):** the game's text routine (`func_800382B4`) is hooked, and its font 0 (menus, the Try Again / Exit / Training menu, the original HUD's numbers) is now drawn in BlockPlanet's font at the game's own positions. Font 1 (the cards) done too: it types its text out word by word (the empty first frames were the typing, not a different encoding). Cards read as text (words joined into lines). Still to do: font 2,, the pictures with text in them (the Try Again hint). Alignment is done: each string is fitted to the width the game's font would give it.

**Records without name entry (decided 2026-11-07).** Profiles already have a name, so record screens use the profile's name instead of asking for one (the original's name entry stays only for its cheat codes, if kept at all).

**Friends' scores online (idea, 2026-11-07).** When online play arrives, friends added by friend code sync their records, and the records boards show them: a switch between this PC's profiles and all friends, and a way to remove friends from earlier games.

**Built-in crash reports (to consider after the first PC test).** A crash handler that writes a dump and a short report into `diagnostics` (Windows: `MiniDumpWriteDump`; Linux: a backtrace), so testers need no registry setting. Until then, `docs/qa/pc-test-checklist.md` explains Windows' own crash dumps.

**Credits (reminder, 2026-11-07).** When the credits are done, add a section for the repositories BlockPlanet builds on or referenced (Blocksphere, N64Recomp, N64ModernRuntime, RT64, RmlUi and others in THIRD_PARTY_NOTICES.md), and the owner's Git username.

**Animated title characters (nice to have, idea 2026-11-07).** Turbine and Wheels are held on one frame of their scenes; letting them move (an idle loop: tentacles swaying, a wheel turning, the Bombs drifting) would bring the title to life. The scene code already animates them; what is needed is a short loop that stays inside the ring, and Wheels' own scene.

**Training laid out for wide screens (idea 2026-11-07).** Training's sphere is drawn narrower than it is tall: the game's own Training camera squeezes it (checked with BlockPlanet's sphere off, `BLOCKPLANET_SPHERE=original`), to fit a 4:3 screen beside the card and Wheels. On wide screens there is room for a round, larger sphere, with the card panel and Wheels beside it. This needs the Training camera's projection found and set to the true aspect, and the card panel (above) placed on its own.

**Sharper textures on the 3D models (idea 2026-11-08).** No HD texture packs (AGENTS.md), so nothing is shipped. Options, from the player's own ROM:
1. **Filtering** (Graphics > Texture filter): smoother, no new detail. Already available.
2. **Upscaling by algorithm as textures load:** a pixel-art upscaler (xBRZ or ScaleFX) enlarges each texture two to four times, keeping edges crisp, on the player's machine. This is the recommended option: it needs a hook in RT64's texture loading, and a Graphics setting to switch it.
3. **BlockPlanet's own shading** for surfaces whose texture is really a material (bricks, metal), as sprites are being replaced by models: sharp at any size, more work per item.

**Animated scenes in the card panels (nice to have, idea 2026-11-07).** The game's cards (Lines' rules, Hide & Seek's goals) sit on a still picture. BlockPlanet's own card panel will be a plain tinted panel in the menus' style (the owner's call: consistent). Later, a small animated scene inside the panel (the picture's subject moving, or one of the game's 3D models turning) would bring the cards to life. It is hard: the scene has to be drawn inside a UI panel and kept clear of the text, so it waits until everything else is done.

**BlockPlanet's own computer opponent (idea 2026-11-08; needed for new versus modes).** The game's computer opponent plays only Versus. BlockPlanet's new versus modes (Lines versus, Multiphasic and others) need one too, and a better one would improve Versus itself. The plan: read the board directly each turn (every brick's shape and colour, the shadow, filler blocks, the core), try each drop and slide the held brick allows, and score them (combo size, digging towards the core, converting filler blocks, setting up 20+ combos for magic, and using magic against the player); look a few moves ahead. Difficulty as a personality, not only speed: an easy opponent takes the first match it sees and pauses, a hard one plans combos; both make human-like mistakes. It plays through the controller, pressing buttons as a player does, so the game's own rules apply unchanged. First needs the rules in code terms (what a drop needs, how combos chain, what slides allow, filler conversions), the same research as Training's "your turn" steps. One opponent serves every mode, with each mode's goals as its scoring.

**Effects built into the music (to do, the owner's find 2026-11-08).** The intro's whooshes and impacts play only with the music: with Music at 0 and Effects at 100 it is silent, so they are part of its music track. N64 music is sequenced on up to 16 channels, each playing an instrument or a sample; the intro's effects are probably on channels of their own. Plan: list the track's channels and the sample each plays, then set each channel's volume while the track plays (the sound library has a per-channel volume), so effect channels follow the Effects level and the rest the Music level. Channels that mix both would limit it. The same may apply to the endings and cutscenes.

**A demo of the focused mode behind the menus (nice to have, idea 2026-11-08).** While a menu is open, the background could play a demo of the mode (archetype) in focus: Story, Hide & Seek, Puzzle, Lines, Versus. Until then, the game's own attract demo should not start behind BlockPlanet's menus (it shows the original HUD); the menu background stays still.

**The H2O logo at the start (reminder for the intro work, the owner 2026-11-08).** The animated H2O Entertainment logo plays at the start of the game's credits. It belongs at the beginning of the game instead, with the intro.

**Logos at the end of the credits (later, with the intro work; decided 2026-11-08).** The credits stay text only for now. Found: the intro's background is one 320 × 240 intensity picture (drawn from `0x8019CC10`, format 3) holding a tilted, embossed pattern of the Nintendo oval logo and the H2O logo (the ring through the "2O"); lifted out, each is only about 70 pixels wide, so Nintendo comes out soft and H2O breaks up. The sharp route: render the H2O logo's best frame (ball, disc and its "H2O ENTERTAINMENT" wordmark, from the game's own credits animation) and the N64 logo model (the intro's model at `0x801B6BE0`, six parts) into pictures, from the player's ROM, and show them at the end of the scroll like a film. Not from outside sources (trademarks).

**A fast, flat-shaded mode inspired by the 1995 prototype era (idea 2026-11-08, much later).** The owner's idea, after H2O's Atari Jaguar prototype that became Tetrisphere: a separate mode with no sliding, fast drops and instant matches straight to the core, a stark flat-shaded glowing look in dark industrial colours, its own sub-modes and local and network versus. Notes: details of the prototype (its HUD, tiles, colours) should come from footage the owner studies (screenshots and notes), not be assumed; and it should be BlockPlanet's own original mode (its own name, palette and tile shapes) inspired by that era, not a recreation of the prototype. Build: BlockPlanet's own rules engine in C++ (needed for network play, where both machines run the same deterministic simulation and exchange only inputs; attacks and garbage as events in that shared timeline), building on the rules research and the computer opponent; a selectable theme for BlockPlanet's sphere and HUD (flat colours, layer depth by brightness, glowing edges).

**Mouse and keyboard controls (sooner rather than later, the owner 2026-11-08).** In play: hold the left button and move to turn the sphere; a quick left click drops the brick; hold the right button to slide (drag); the mouse wheel zooms in and out; Space fires the magic; W, A, S and D for the other actions; Q and E to peek left and right (a new feature, like peeking round a corner in a shooter; later); Esc for back and pause. In menus: a click chooses, the wheel scrolls lists and text panels. Needs: mouse input into the game's controls (turning by mouse movement, click versus hold), a click and hover layer for BlockPlanet's menus, and a matching Controls screen and prompts.

**Losing a life (to design with the owner, 2026-11-08).** The original's skull is gone in Story. Suggested: the lost heart shakes, cracks and drains from red to an empty outline (about half a second), the Health panel flashes red once, and the drop timer bar visibly resets; optionally a brief red pulse at the screen's edges.

**Resuming a game (to discuss with the owner at the PC, 2026-11-09).** The owner's idea: the first item under Solo is "Resume <game type>" (for example "Resume Story · Stage 3:4" or "Resume Story · Episode 3"), remembering only the last game, instead of Continue on the main menu. Questions: how a single stage or a whole episode shows; whether the chapter or stage list marks the one in progress; what happens when something else is started while a game is saved.

**One grid background for the menus (the owner's design, 2026-11-09).** Keep the game's 9 × 7 tiled grid, but as one background BlockPlanet draws itself (the game's marble tile texture from the player's ROM, tinted; each tile a piece placed and tilted so the grid bulges like the sphere). It sits above the game's picture and HUD and below BlockPlanet's menus, so it also covers the old menus, demo and artefacts. Between screens of the same colour it stays still; to a new colour the far background fades and the tiles flip over in a ripple to show the new colour; into a screen without it (play) the tiles fly towards and past the camera (joined with the loading cover); from such a screen they fly in. Colours: blue (main menu, Options, Profiles), green (Solo, Story, mode lists), red for game over (once the GAME OVER models have shown for a second or two, behind BlockPlanet's end panel), and a few more for flexibility (for example one per mode). Steps: (1) the grid and colour changes between menus; (2) the fly-out and fly-in, with the loading cover and the game over panel. Step 1 built 2026-11-09 on the branch `menu-grid`; after the owner's first test: 8 × 8 tiles on a rounder sphere, an idle wave, the pause menu on the grid (a colour cycle), and fixes for fast back-and-forth and the credits' stutter (docs/UI.md, Menu grid). Step 2 (flying in and out, with the loading cover, the pause and Quit) built 2026-11-09 on the same branch. The idle motion (tiles bobbing on water, measured against the original with `tools/qa/grid_motion.py`) is accepted for now (the owner, 2026-11-09); final tuning deferred until much later, on the grid test screen. Known differences then: the grid is smaller than the original's background, which fills a 4:3 screen's width (ours is sized by the shorter side, the owner's choice), and its light sweeps across the grid as a wave (tile to tile about 0.65 s) where the original's changes per tile almost at once (about 0.06 s); lighting each tile by its tilt was tried and measured further away. Still to do: the title screen on the new grid (needs the grid under the game's picture, or the title rebuilt), game over (red tiles flying in once GAME OVER has shown), and perhaps an idle motion per section.

**From the fan research (2026-11-09; [research/fan-feedback.md](research/fan-feedback.md)), with when to start each.** The two most repeated points are the learning curve and the cursor; the soundtrack is the most loved feature.

| Item | What | When to start |
| --- | --- | --- |
| Cursor clarity | Make it unmistakable whether a drop is allowed and whether the cursor holds a brick for sliding (outline, colour, a short sound), and say why a drop was refused. The second most common complaint. | **Next**, after direct launch's Story step: it needs the drop rules, which the rules research and direct launch uncover. |
| Input responsiveness | Check the report that the game handles input only about every 5 frames; if so, read input every frame on BlockPlanet's side for cursor movement only, without changing simulation speed or timers (AGENTS.md). Measure first. | **Next**, together with cursor clarity (one session). |
| Cursor speed and drag strength | Expose the original's cursor Speed and Power settings clearly in Options, with descriptions. | **Next**, with the two above (small). |
| Rendering checks | Test what emulators get wrong: seams between 2D pictures (menus, HUD, intro), piece colours always consistent, the title's sprites, fog at the start of Rescue. | **Soon**, a short check alongside the style and grid work. |
| Jukebox and VORTEX | The jukebox (track names, music choice) is high value given how loved the soundtrack is; add the VORTEX name code (an animation) there, perhaps as a visualiser. | **After direct launch**, with the Media Player item. |
| Lines multiplier glitch | A reported trick pushes Lines' multiplier past 20x: decide to keep it or cap it, and keep records consistent. | **Before Lines gets records and trophies.** |
| Practice and the original Time Trial | Keep the original Practice settings (pieces, layers, core area, no time limit) reachable as a learn-at-your-own-pace space, and the original Time Trial somewhere. | **With the rulesets** (stages 2 and 3). |
| Rescue variety | "A little vanilla" and "repetitive": new modes and rule presets matter for long-term play. | **With the rulesets** and Multiphasic. |
| Debug strings as clues | The ROM's leftover developer messages from level generation and drop checks can help map those rules (read at research time only, never committed). | **With the rules research** (the opponent, Training's "your turn" steps). |
| European ROMs | The missing track and the music-list glitch are easy fixes in BlockPlanet's jukebox; check how a 50 Hz ROM is paced first. | **Later, only if European ROMs are supported.** |

Already done or planned and confirmed by the research: Training, How to Play and the glossary (the learning curve); BlockPlanet's HUD and combo counter; the peek camera, Full sphere detail and see-through core (hidden pieces, the point the Tetris Company's Henk Rogers singles out); Endless Score Attack, seeds, daily stages and trophies (repetition); local two-player VS (should not slip far). Keep as they are: the core rules, the fixed-camera sphere feel, the big chain reactions, the soundtrack as default music, and a gentle start with Training.

**Priority: launch modes directly, without the original menus (the owner, 2026-11-09).** BlockPlanet's menus are its own, but a game is still started by pressing through the original menus hidden behind (`ModeLauncher`: Single, the player page, the mode page), so their grids, models, sounds, attract demo and timing leak through. Instead, for each mode, set what the original menus would (mode, chapter, stage, difficulty, save slot) and call the game's own start routine, so the old menus never run. Steps: (1) Story: record every value the menus set before play (a memory watch from the main menu to play, compared with a direct start), reproduce it, and compare play started both ways; (2) switch Story to the direct start, keeping `ModeLauncher` as a fallback; (3) the same for Training, Puzzle, Hide & Seek, Lines, Versus and Time Trial; (4) boot straight into BlockPlanet (intro, title, Who is playing?, its menus) so the original menu code is never reached. The menus stay as they are; only the start changes. The loading cover and the grid's fly-out then become presentation only. Related: holding the GAME OVER scene (the same kind of research).

**One game, many rulesets (the owner's idea, 2026-11-09; long term).** Instead of each mode being its own path, one game whose rules come from a ruleset chosen in the menus; Story, Hide & Seek, Puzzle, Lines, Versus and Time Trial become presets, and new modes (Custom, Multiphasic, a fast flat-shaded mode, new versus modes) new combinations. Every variable of every mode: layers, block types, the objective, a single stage or an episode, the background effect, the default music, hidden pictures, whether the core breaks and how much of it must be uncovered, lives, the drop timer's speed, a shot time limit, score and speed goals, a friend inside, the playfield's size and shape, and the computer opponent's intelligence level. Variables are set per stage, not only per mode (the owner's note): an episode is a list of stages, each with its own values, so for example the opponent can get smarter stage by stage. Stages: (1) a catalogue of what each mode has and does, and has not and does not, with where the game keeps each variable and each mode's values (starting from docs/research/modes-matrix.md; the direct-launch work finds most of them); (2) a ruleset format, read from data files, with the built-in modes written out to reproduce the original exactly; (3) apply rulesets to the game's own engine wherever a variable maps to something it supports; (4) playfield shape and size need BlockPlanet's own game engine (the original's board is a fixed 32 × 32 grid per layer round a sphere), the same engine network play and the fast mode need. Order: direct launch first, then 1 to 3, with 4 as the long-term goal.

**The grid test as an unlockable secret (nice to have, the owner 2026-11-09).** The grid test screen (Options > Grid test, built for tuning the menu grid) could become a hidden "dev secret" players unlock (a trophy, a code, finishing the game) to customise the menu background: colours, breathing, wave and fly-in, within safe limits (no flashing faster than photosensitivity guidance allows, no values that hide the menus or make them unreadable), saved per profile, with a "Reset to default". Until then it stays a tuning screen.

**Play for a trophy (nice to have, idea 2026-11-08).** Opening a trophy on the Trophies screen offers "Play for trophy" where the place to earn it is known, and goes straight there: the Training trophy opens Training, a Story trophy its episode, a Puzzle trophy the puzzle list, a Hide & Seek trophy Hide & Seek. Trophies earned anywhere (a score, a combo) offer nothing, or the mode they are most often earned in. Each trophy definition (`assets/trophies/trophies.json`) would carry the command that starts its mode, the same commands the menus use.

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
