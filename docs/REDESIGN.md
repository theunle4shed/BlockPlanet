# BlockPlanet redesign proposal

A critique of Tetrisphere and a proposal for a modern reimagining, written as a studio design document. The original is the foundation, not a constraint: keep what made it memorable, and modernise systems, presentation, progression, accessibility and replayability.

This proposal builds on what BlockPlanet has already learned about the original game ([research notes](research/blockplanet-game-state.md), [HUD inventory](HUD_INVENTORY.md)) and on decisions already recorded in the [roadmap](ROADMAP.md). Each section ends with a **feasibility** note. BlockPlanet runs the original game code, so some ideas are host-side work, some need hooks into the game, and a few would need an engine of our own.

## Design pillars

1. **The sphere is the instrument.** Every system serves the act of reading, rotating and digging into a sphere. Presentation should feel like playing a visualiser, not a toy.
2. **Pressure without panic.** The original's tension came from time and a few hearts. Keep the tension, remove the unfair moments (blocks you cannot see, junk you cannot answer).
3. **Always a next goal.** Every session should end with something unlocked, improved or nearly beaten.
4. **Readable at any size.** From a 32:9 monitor to a handheld in portrait, the sphere and the information around it must stay clear.
5. **Accessible by default.** Difficulty, motion, colour and flashing are player choices, not hurdles.

## 1. Visual identity

**Why the original worked.** Tetrisphere's backgrounds were its signature: swirling, colour-shifting, music-driven abstractions behind a solid, tactile sphere. Neil Voss's soundtrack and those visuals gave it an atmosphere unlike any other puzzle game of its time.

**Limitations.** The backgrounds were fixed animations, not truly reactive to the music. The 1990s cartoon presentation (mascot robots, rounded comic fonts, bright primary-coloured menus) undercut the hypnotic, electronic atmosphere and dates the game badly. Low resolution and N64 texture limits flattened the effects.

**What changes.**
- Remove the cartoon mascots from menus, results and the HUD. If characters remain anywhere (VS identities, custom characters), present them as abstract avatars or emblems, not cartoons.
- Replace the comic fonts with a clean technical typeface: a geometric sans for headings and a monospaced or tabular figure set for numbers, so scores and timers do not jitter.
- Restyle the UI with a restrained neon and holographic palette: thin lines, glow used sparingly, glassy panels, generous dark space. Glow is an accent, never the main way information is shown.

**What is added.**
- Audio-reactive backgrounds inspired by MilkDrop, Geiss, G-Force and VJ tools: beat, bass and treble drive motion, colour and geometry, from both the game's music and the player's own (see the Media Player).
- Visual intensity levels, so the background never competes with the sphere: a calm mode during play, full intensity in menus, the media player and between levels.
- A visual language for state: the background reacts to combos, danger (low hearts, time running out) and chapter progress.

**Player benefit.** A distinctive, adult identity that is instantly recognisable in screenshots and streams, and an atmosphere that rewards long sessions rather than tiring them.

**Feasibility.** Fonts, menus and HUD art are host-side and texture-replacement work (Phases 2 and 3 of the roadmap). Music reactivity needs the background research already planned: the game renders each background off-screen at RDRAM `0x000400` and draws it as 40 tiles, so it can be driven or replaced. Removing mascots from 3D scenes such as the VS intro means skipping or replacing those scenes.

## 2. Camera

**Why the original worked.** The fixed, centred sphere made every rotation predictable, and turning the sphere under a fixed camera was intuitive with a stick.

**Limitations.** The framing was designed for a 4:3 television. Blocks near the edge of view go missing or show the layers beneath, because the game only draws a fixed patch of the sphere around the cursor (confirmed in BlockPlanet's research and on real hardware). There is no way to see the far side.

**What changes.**
- The sphere's resting framing always fits the screen at any aspect ratio, and never exceeds 80% of the screen's width or height (the smaller of the two).
- Ultrawide, standard, square and vertical layouts are first-class. The HUD moves to the long edges of the screen, so the sphere keeps the centre.
- Draw every block with an exposed face, so nothing goes missing near the edge (the "Extended" and "Full" sphere detail steps in the roadmap).

**What is added.**
- A gentle push towards the sphere while a piece falls, returning to the default framing after placement. It adds focus and weight to each drop.
- A peek camera: hold a button to look at the back of the sphere, without changing game state.

**Changes to the original intention.** Make the push subtle (a few percent of zoom, eased) and switch it off with "Reduce camera motion". Zooming on every drop can cause motion sickness and makes repeated fast play harder to read. Consider zooming only on the final part of a long fall, or only on large combos.

**Player benefit.** The sphere always reads clearly on any screen, nothing important is ever hidden by the renderer, and drops feel more physical.

**Feasibility.** Framing and aspect support are mostly RT64 projection and HUD anchoring work. Drawing every exposed block means enlarging the game's sphere patch (research in progress). The push and peek cameras need the game's camera values, which are not found yet.

## 3. Story and progression

**Why the original worked.** Rescue gave a simple goal (dig to the core) with escalating chapters, and the sphere's growing depth was a clear measure of difficulty. Each level was quick to understand.

**Limitations.** Progress was thin and opaque. Unlocks were hidden behind typed codes (LINES, CREDITS, level select). There was no visible map of progress and no reason to replay cleared chapters. Losing all hearts sent you back without much sense of what was earned.

**What changes.**
- Rename Rescue to **Story**. Chapters unlock in order and stay selectable once unlocked, as already decided for chapter level select.
- Hearts and stored magic persist across the levels of a chapter and reset at the start of each chapter. That makes each chapter a self-contained run with its own tension, and a failed chapter costs minutes, not an hour.
- Remove the typed secret codes. Their content becomes earned unlocks (already decided).

**What is added.**
- A chapter map: tiles per chapter with best score, best time, stars or medals, and the content each chapter unlocks.
- Chapter rewards: modes (Lines, Custom), media player content, visualiser presets, and cosmetic sphere themes.
- Optional per-chapter challenges (no magic, no hearts lost, under par time) that award medals and trophies.

**Player benefit.** A clear sense of direction, short and fair runs, and reasons to return to finished chapters.

**Feasibility.** Chapter unlocks, the map and rewards are host-side, using the chapter and level values already found (`0x800E44A0`, player context `+0x23BA`). Persisting hearts and magic between levels and resetting them at chapter starts needs game-side hooks on the hearts value (found) and on magic (not yet found).

## 4. Menu structure

**Why the original worked.** Few menus, quick to reach play.

**Limitations.** A deep tree of small cartoon panels, slow transitions, name entry used for secrets, and modes split confusingly between Single, Practice and Training.

**Proposed structure.**

```
SOLO            Story · Time Trial · Lines · Puzzle · Custom · Training
MULTIPLAYER     Versus · Co-op · Custom          (local or online)
MEDIA PLAYER
TROPHIES
OPTIONS
CREDITS
EXIT
```

**Changes to the original intention.**
- Keep **Puzzle**. It is the original's most distinctive mode after Rescue, and it suits short sessions.
- Fold **Practice** into Custom (choose chapter, depth, speed, no fail), and keep **Training** as the onboarding path rather than a menu destination players must find.
- **Hide & Seek** becomes a Story level type or a Custom rule set rather than a top-level mode.
- Name the cooperative mode **Co-op**. "Collaborate" reads like a productivity tool.

**Player benefit.** Every activity is one or two steps from the main menu, and the names say what each mode is.

**Feasibility.** The new menus sit on the planned RmlUi layer and drive the original modes through their mode numbers (1 to 6, already mapped). Co-op needs new game logic (see section 7).

## 5. Time Trial

**Why the original worked.** A pure score chase against a five-minute clock, ideal for "one more go".

**Limitations.** No records beyond a local results screen, nothing to compare against, and no way to practise a specific level.

**What changes.**
- Time Trial can replay any completed Story level with fixed lives, alongside the original survival format.
- Personal bests per level, with splits.

**What is added.**
- Friends' bests from friend codes, and developer benchmark times (bronze, silver, gold and "dev").
- Seeded levels. Levels are generated randomly, so a shared seed gives everyone the same sphere. That makes comparisons fair and enables a **daily challenge**: one seed per day, one attempt counted.
- Ghost data, if replays prove deterministic (see risks).

**Player benefit.** Clear targets at every skill level and a reason to revisit levels daily.

**Feasibility.** Records and benchmark times are host-side. Seeding needs the level generator and the game's random number state (Blocksphere mapped the global RNG at `0x800E2AA0`). Friends' bests need the online service planned for multiplayer.

## 6. Versus

**Why the original worked.** Two spheres side by side, with combos that hurt your opponent, created direct, readable rivalry.

**Limitations.** Two players only, local only, little control over rules, and attacks that can feel random or overwhelming.

**What changes and what is added.**
- Local split screen and online play, with friend-code rooms (already decided), up to four players.
- The host selects chapter or difficulty, and the format: best of 1, 3 or 5.
- Random level selection from chosen pools, using shared seeds so every player gets an equal sphere.
- Handicaps per player (depth, speed or junk received), so mixed-skill groups stay competitive.

**Player benefit.** Flexible, fair matches for friends of any skill, at home or online.

**Feasibility.** Formats and rules are host-side settings. Four players means each player runs their own game with attacks sent between games (as planned), not four spheres in one game. Shared seeds need the generator research above.

## 7. Junk system

**Why the original worked.** Combos created pressure on the opponent, turning a solo puzzle into a duel.

**Limitations.** Attacks can arrive at awkward moments and can cascade into losses the receiver cannot answer.

**Kept from the original intention.**
- Junk amount scales with combo size, in small quantities.
- Junk is delivered after the sender's move completes, never during the receiver's active placement.
- Junk rarely causes instant failure.
- Junk looks cracked and corrupted, never contributes to combos, and can never create junk in return.

**Changes to the original intention.**
- **Do not pause the receiving player.** Pausing interrupts flow and invites abuse in online play. Instead, queue incoming junk and deliver it at the receiver's next placement, with a visible incoming meter so the attack can be anticipated.
- **"Prefers active digging areas" needs a limit.** Targeting the hole a player is digging feels personal and can be crushing. Bias towards the digging area, but cap junk per area and never fill a hole completely.
- **Add counterplay.** A combo made while junk is queued cancels part of the incoming junk before it lands (the "offset" rule from modern competitive puzzle games). Defence becomes skill rather than luck.

**Player benefit.** Attacks feel fair and readable, comebacks are possible, and skilled defence is rewarded.

**Feasibility.** This needs the original VS attack mechanics mapped first, then game-side hooks to queue, place and mark junk. The cracked look is texture work.

## 8. Co-op (new)

**Proposal.** Two to four players dig the same sphere from different sides, sharing hearts and a score. Combos made together feed a shared magic meter.

**Player benefit.** A friendly multiplayer mode for players who do not enjoy head-to-head competition.

**Feasibility.** The hardest item in this document. The original game has one cursor per sphere, so several players on one sphere needs new game logic, possibly an engine of our own. Recommend prototyping after Versus.

## 9. Media Player

**Why the original worked.** Its soundtrack and visuals were strong enough that players left the game running just to watch and listen.

**Limitations.** No way to listen or watch outside play, and the visuals were not interactive.

**What changes and what is added.**
- A full visualiser mode: game music or the player's own files (MP3, OGG, FLAC), driving audio-reactive backgrounds.
- Interactive 3D objects (spheres, magic items, abstract forms) that the player can rotate, zoom and move, swapping periodically.
- Presets the player can save, and preset packs earned through Story.
- A clean "presentation mode" with the UI hidden, for streaming or ambient display.

**Player benefit.** A long-term reward that extends the game beyond play, and a natural showcase for its visual identity.

**Feasibility.** Mostly host-side: audio decoding and analysis, a shader-based visualiser and a simple 3D viewer on the new UI layer. The game's own music needs the song table research (jukebox item in the roadmap).

## 10. User interface

**Why the original worked.** Big, simple choices that were easy with a controller.

**Limitations.** Slow transitions, inconsistent layouts, cartoon styling and no progress information.

**What changes and what is added.**
- TV-first navigation (already decided): large focus states, few items per screen, transitions under 200 ms.
- Chapter grids and mission tiles, completion tracking, difficulty indicators, performance statistics and a progression map.
- One consistent layout system that scales from vertical to ultrawide.

**Changes to the original intention.** "Mobile-style" grids work well when designed for a controller first: a clear focus highlight, predictable D-pad movement and no swipe-only actions.

**Player benefit.** Faster access to play, and progress that is always visible.

## 11. Accessibility

Not in the original brief, but essential for a modern release.

- **Photosensitivity:** a flashing and strobe limiter for visualisers, a calm background option, and a startup warning. Audio-reactive visuals are the highest risk in this design.
- **Colour:** block pairs differ mainly by colour (two colour types per chapter in the original). Add colour-blind palettes and optional patterns or symbols on block types.
- **Motion:** reduce camera motion, sphere rotation smoothing, and an option to turn off screen shake.
- **Difficulty:** casual play with no game over (already implemented as `--casual`), adjustable fall speed, and assist options such as a longer drop guide. Assist modes pause trophies, as casual play does now.
- **Input:** full remapping (inherited from Blocksphere), hold or toggle choices, and one-handed layouts.
- **Readability:** text size options, high-contrast HUD, and audio cues for low hearts, incoming junk and time warnings.

## 12. Long-term engagement

- **Trophies:** built in and ready for launcher integration (implemented; 19 proposed).
- **Daily challenge and weekly seed** with friends' leaderboards (needs seeding).
- **Custom levels:** share designed spheres with a short code (the generator research supports this).
- **Cosmetics earned through play:** sphere themes, block skins, visualiser presets. No paid randomised rewards.
- **Seasonal events:** optional, low-pressure, built from seeds and presets rather than new content drops.

## What to keep, change or drop

| Idea | Recommendation | Reason |
| --- | --- | --- |
| Audio-reactive visualiser backgrounds | **Keep** | The heart of the identity. |
| Remove mascots, mature typography, neon UI | **Keep** | Matches the "modern only" decision; use glow sparingly. |
| Sphere always fits, at most 80% of the screen | **Keep** | Essential for all aspect ratios. |
| Camera push on falling blocks | **Change** | Subtle, optional, and tested for motion sickness. |
| Chapters persist, resources reset per chapter | **Keep** | Short, fair runs. |
| Menu structure | **Change** | Add Puzzle, fold Practice into Custom, rename Collaborate to Co-op. |
| Time Trial bests, friends and dev times | **Keep** | Add seeds and a daily challenge. |
| Versus formats, pools, host settings | **Keep** | Add handicaps and shared seeds. |
| Pause the receiver during junk placement | **Drop** | Breaks flow; queue to the next placement instead. |
| Junk prefers active digging areas | **Change** | Bias with a cap; never fill a hole completely. |
| Junk counterplay | **Add** | Combos cancel incoming junk. |
| Media player as a visualiser | **Keep** | Add a presentation mode and earned preset packs. |
| Mobile-style grids | **Change** | Controller-first grid design. |
| Co-op on one sphere | **Keep, later** | Highest-risk system; prototype after Versus. |

## Risks and constraints

- **Trademarks.** "Tetris" and Tetris trade dress are strictly enforced by The Tetris Company. Keep the BlockPlanet name and avoid Tetris branding in a public release.
- **Original game material.** BlockPlanet runs the original code and needs the player's own ROM. New art, music support and modes are fine; redistributing original assets is not.
- **Determinism.** Ghosts, replays and lockstep online play need the game to replay exactly the same way from the same inputs. The current runtime is not proven deterministic. Seeds and attack messages are safer foundations.
- **Engine limits.** The original game logic has one player per sphere, one cursor, and a fixed draw patch. Co-op and a full free camera may need replacing parts of the game rather than hooking them.
- **Photosensitivity.** Audio-reactive visuals need flash limits and testing before release.

## Suggested order

1. Visual identity foundations: new UI layer, typography, HUD art, calm and full background modes.
2. Camera framing for all aspect ratios, then the extended sphere patch.
3. Story structure: chapter map, unlocks, per-chapter resources.
4. Media Player, from the jukebox and visualiser research.
5. Time Trial records and seeds, then the daily challenge.
6. Versus formats and the junk system, local first, then online.
7. Co-op prototype.

## Open questions

- Which existing characters, if any, survive as abstract avatars for VS and custom characters?
- How many chapters should Story have? The original's real chapter count is still being measured.
- Should Hide & Seek stay as its own mode or become a Story level type?
- What reward does finishing Story give: Media Player, Lines, Custom, or all three?
- Should online leaderboards be friends-only, matching the friend-code decision?
