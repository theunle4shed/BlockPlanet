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
- **See through an opened core.** When the core is opened, the undersides of the blocks on the far side of the sphere become visible through the hole, dark but readable. The original never draws the far side, so the opening shows nothing behind it. Showing it makes the sphere feel solid, and lets players read the far side before rotating to it.

**Changes to the original intention.** Make the push subtle (a few percent of zoom, eased) and switch it off with "Reduce camera motion". Zooming on every drop can cause motion sickness and makes repeated fast play harder to read. Consider zooming only on the final part of a long fall, or only on large combos.

**Player benefit.** The sphere always reads clearly on any screen, nothing important is ever hidden by the renderer, and drops feel more physical.

**Feasibility.** Framing and aspect support are mostly RT64 projection and HUD anchoring work. Drawing every exposed block means enlarging the game's sphere patch (research in progress). Seeing through the core needs the far side drawn as well (the "Full" sphere detail step), with the inward-facing block faces darkened, for example by lighting them with depth so the far side reads as distant and in shadow. The push and peek cameras need the game's camera values, which are not found yet.

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

**Structure (decided 2026-11-06).** Three levels: who you play with, the game (archetype), then how you play it (sub-mode). Each item shows a short description of what it is when focused. A level with a single choice is skipped (Lines has one sub-mode today, so Solo > Lines starts it). The levels are open to expansion: new archetypes, sub-modes, or a further level where a mode needs one.

```
CONTINUE        only while a Story or Casual game was left unfinished
SOLO            Story · Hide & Seek · Puzzle · Lines · Multiphasic (later)
  Story         Campaign · Time Attack · Score Attack · Casual
                  then an episode, then Whole episode or one stage
LOCAL           Story · Lines (later) · Custom (later)
  Story         Versus · Cooperative (later)
  Lines         Versus · Cooperative (both later)
                  then player 2: a second player or the computer
ONLINE          Host a game · Join with a code (later)
PROFILES · MEDIA PLAYER · TROPHIES · OPTIONS · CREDITS · EXIT
```

- **Archetypes:** Story, Lines and Multiphasic are the games that can grow multiplayer and sub-modes. Puzzle and Hide & Seek are archetypes with their own level sets, seen under Solo only for now.
- **Versus and Cooperative are sub-modes** of an archetype under Local (and Online), as Campaign and the attacks are under Solo: Story's Versus is the original's VS; Cooperative (renamed from Collaborate, 2026-11-06) is two players on one sphere (section 8).
- **Local is two players for now** (as the original, which had two): player 1 is already in the game, and player 2 is a person on a second controller or the computer. Every local game offers the computer as player 2, though some will only gain it later (a computer partner for Cooperative needs designing). Four local players can come later if wanted.
- **Casual** is a sub-mode chosen from the menu: Story without a game over (hearts come back), no records or trophies.
- **Continue** (revised 2026-11-06): a checkpoint at the start of each Story or Casual episode, with the score and hearts then; Continue starts that episode again from its first stage with them. A stage left part-way is **not** resumed: the game's exact state cannot be restored (it would need a full snapshot of the running game), and restarting a stage would let a player quit just before losing and keep their progress. The game's own end-of-episode menu (Continue, Exit) serves as "save and quit": the checkpoint is taken as the next episode starts. Held magic is not restored (writing it skips the game's set-up). Cleared at a game over. Not for Time Attack or Score Attack.
- **Join codes (online, later):** shown hidden (`••••••`) with a button to reveal them and a **Copy** button that works while hidden, and a **streamer mode** setting that hides codes everywhere by default (lobby, notifications), perhaps friends' names too.

The earlier proposal (SOLO: Story, Time Trial, Lines, Puzzle, Custom, Training; MULTIPLAYER: Versus, Cooperative, Custom) is replaced. Training is on each archetype's X button (Story: Basics and Advanced; Puzzle: Puzzle Training; 2026-11-07, replacing Chapter 0); Practice's settings become Custom later.

**Changes to the original intention.** Make the push subtle (a few percent of zoom, eased) and switch it off with "Reduce camera motion". Zooming on every drop can cause motion sickness and makes repeated fast play harder to read. Consider zooming only on the final part of a long fall, or only on large combos.

**Player benefit.** The sphere always reads clearly on any screen, nothing important is ever hidden by the renderer, and drops feel more physical.

**Feasibility.** Framing and aspect support are mostly RT64 projection and HUD anchoring work. Drawing every exposed block means enlarging the game's sphere patch (research in progress). Seeing through the core needs the far side drawn as well (the "Full" sphere detail step), with the inward-facing block faces darkened, for example by lighting them with depth so the far side reads as distant and in shadow. The push and peek cameras need the game's camera values, which are not found yet.

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
MULTIPLAYER     Versus · Cooperative · Custom    (local or online)
MEDIA PLAYER
TROPHIES
OPTIONS
CREDITS
EXIT
```

**Changes to the original intention.**
- Keep **Puzzle**. It is the original's most distinctive mode after Rescue, and it suits short sessions.
- Fold **Practice** into Custom (choose chapter, depth, speed, no fail), and keep **Training** as the onboarding path rather than a menu destination players must find.
- **Hide & Seek** becomes a Story level type and a rule set for multiplayer: in **Cooperative**, two players uncover one picture together; in **Versus**, both race to uncover the same picture (shared seed) or the most of it before time runs out.

**Player benefit.** Every activity is one or two steps from the main menu, and the names say what each mode is.

**Feasibility.** The new menus sit on the planned RmlUi layer and drive the original modes through their mode numbers (1 to 6, already mapped). Cooperative needs new systems on top of the game (see section 8).

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
- Local split screen (two players for now, player 2 a person or the computer) and online play, with friend-code rooms (already decided), possibly up to four players later.
- The host selects chapter or difficulty, and the format: best of 1, 3 or 5.
- Random level selection from chosen pools, using shared seeds so every player gets an equal sphere.
- Handicaps per player (depth, speed or junk received), so mixed-skill groups stay competitive.
- A **Hide & Seek race:** both players uncover the same hidden picture (shared seed); first to finish, or most uncovered when time runs out, wins.

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

## 8. Cooperative (new)

Two players work on one shared sphere at the same time, with no split screen, no minimap and minimal UI. Players should always know where their teammate is, what they are doing, and what they intend next, without voice chat.

**Why it fits Tetrisphere.** The original's sphere is a single, readable object with one point of action (the cursor and the falling piece). That focus is what makes a shared screen possible: two players can read the same sphere at a glance, where two flat Tetris wells would compete for attention.

**Limitations to design around.**
- The original has one cursor and one falling piece per sphere, and the sphere turns to keep that cursor centred. A second player cannot also rotate the sphere without fighting the first.
- The original player can only drop pieces, slide blocks and use magic. A support role needs its own meaningful actions, or it becomes a spectator with a pointer.
- Identity by colour alone fails for colour-blind players and in busy, glowing scenes.

### Roles

| Role | Responsible for | Controls |
| --- | --- | --- |
| **Drop player** | Moving and placing the falling piece, sliding blocks, driving progress | The original game controls. The camera follows this player. |
| **Support player** | Using the team's magic, preparing combos, opening paths, managing resources, pinging | A free cursor anywhere on the sphere, plus support actions |

Roles may become optional later (both players able to drop), but the first version should keep them fixed: it matches the game's single falling piece and keeps the camera calm.

**Support actions (proposed, to make the role meaningful):**
- **Use magic at the support cursor.** Magic belongs to the team; the support player aims and fires it.
- **Hold or swap the next piece.** A new mechanic: swap the upcoming piece with a held one, shaping the drop player's next move.
- **Ping** (below).
- **Later candidates:** a short "brace" that slows the falling piece, or nudging single blocks.

### Player identity

Each player has a permanent identity colour, cyan for player 1 and orange for player 2 by default, used consistently on cursors, highlights, outlines, pings, magic previews and activity indicators.

**Addition for accessibility:** pair each colour with a **shape and line style** so identity never depends on colour alone. For example, the drop player gets a solid ring and solid outlines, and the support player a segmented ring and dashed outlines. Offer alternative colour pairs in options.

### Awareness system

Ordered by importance, as in the original notes:

1. **Player-coloured cursors.** The drop cursor is larger, brighter, with a stronger glow and the highest priority. The support cursor is softer, with less glow, but always clearly visible.
2. **Player-coloured block outlines.** The block under a cursor gets a thin outline in that player's colour, a soft glow and mild rim lighting: passive communication of intent.
3. **Activity zone halos.** Where a player has been working for a while, nearby blocks get a faint tint and a subtle halo that fades after a few seconds, so a teammate can see where work is happening without watching the cursor.
4. **Falling piece highlighting.** The active piece always has enhanced glow, rim lighting and a subtle pulse, with an optional outline: "this is the important block right now".
5. **Pings.** Point at a block and press Ping: a neon pulse, an expanding ring and a temporary marker. For suggesting a combo, marking a target, pointing at a power-up or asking for help. Optionally, a second press on the same spot within a moment changes the ping's meaning ("drop here", "danger"), still without text.
6. **Hidden-side indicators.** When a teammate's cursor is on the far side, a small coloured marker at the sphere's edge and a thin arc along the perimeter show the direction. Subtle, fading with angular distance, never covering play.
7. **Magic previews.** Preparing magic shows a player-coloured targeting overlay, a brief pulse and the area of effect before activation.

**Rules for all awareness effects:** they must never hide a block's type or colour, must respect the reduced-motion and flash-limit options, and must stay readable on the calm background mode.

### Changes to the original notes

- **Camera ownership is decided:** the camera follows the drop player. The support player's cursor moves freely and relies on the hidden-side indicators when out of view. The peek camera is available to the drop player only, so the view never changes without that player's input.
- **Support needs real actions:** magic aiming and the hold or swap piece make the role active rather than advisory.
- **Identity by shape as well as colour.**

### Cooperative tutorial

Cooperative adds roles and signals that solo players have never seen, so it needs its own short, hands-on tutorial that both players play together:

1. **Roles:** the drop player places a piece; the support player moves their cursor and sees where the drop player is.
2. **Signals:** each player pings a block and watches the other's outline, halo and edge marker.
3. **Support actions:** the support player fires magic at their cursor and swaps the next piece.
4. **A first shared level:** a small sphere cleared together, with hints only when needed.

Each step is short, can be skipped, and can be replayed from the Cooperative menu. Hide & Seek levels suit the first shared level, since the goal is visible to both players.

### Player benefit

A cooperative mode that keeps the original's single, focused sphere, gives two players distinct and meaningful jobs, and lets them coordinate silently. It suits mixed-skill pairs: the stronger player drops, the newer one supports.

### Feasibility in BlockPlanet

- **Drop player:** the original game's player, unchanged.
- **Support cursor, outlines, halos, pings and edge indicators:** drawn by BlockPlanet on top of the game. They need the mapping from grid cells to positions on the sphere, the same mapping the culling research is uncovering (the game builds sphere vertices from a precomputed mesh table). Tints on blocks could instead be applied to the vertex colours the game already generates.
- **Falling piece highlight:** needs the falling piece's draw call identified, then a tint or outline. Medium.
- **Magic at the support cursor:** needs the magic system mapped (not yet found), then a hook that fires magic at a chosen cell rather than the drop cursor. Research first.
- **Hold or swap piece:** the piece queue is game state; needs mapping, then a hook. Medium.
- **Online play:** both players share one simulation. The practical route is host-authoritative: the host runs the game, the remote player's support inputs are sent as small messages (support actions are infrequent, so latency matters less than for the drop player), and the remote player's view is kept in step from the host's state. This is different from Versus, where each player runs their own game, and should come after local Cooperative works.
- **Order:** local Cooperative with the support cursor, outlines, pings and edge indicators first; then magic aiming and hold or swap; then activity halos and polish; then the tutorial; online last.
- **Hide & Seek levels** already exist as level records with goal text, so a Cooperative or Versus Hide & Seek level can reuse them.

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
| Menu structure | **Change** | Add Puzzle, fold Practice into Custom, make Training the onboarding path. |
| Time Trial bests, friends and dev times | **Keep** | Add seeds and a daily challenge. |
| Versus formats, pools, host settings | **Keep** | Add handicaps and shared seeds. |
| Pause the receiver during junk placement | **Drop** | Breaks flow; queue to the next placement instead. |
| Junk prefers active digging areas | **Change** | Bias with a cap; never fill a hole completely. |
| Junk counterplay | **Add** | Combos cancel incoming junk. |
| Media player as a visualiser | **Keep** | Add a presentation mode and earned preset packs. |
| Mobile-style grids | **Change** | Controller-first grid design. |
| Cooperative: shared sphere, drop and support roles | **Keep** | The drop player is the original game's player, so it does not need two simultaneous droppers. |
| Cooperative: colour identity | **Change** | Add shape and line style so it never relies on colour alone. |
| Cooperative: support role | **Add** | Magic aiming and a hold or swap piece, so support is active, not advisory. |
| Cooperative: camera | **Decide** | The camera follows the drop player; the support player relies on edge indicators. |
| Cooperative tutorial | **Add** | Roles and signals are new to everyone; teach them by playing together. |
| Hide & Seek in Cooperative and Versus | **Add** | A shared picture suits co-operation, and a race on the same picture is a fair duel. |
| See through an opened core | **Add** | The far side's undersides, dark but visible, make the sphere feel solid. |

## Risks and constraints

- **Trademarks.** "Tetris" and Tetris trade dress are strictly enforced by The Tetris Company. Keep the BlockPlanet name and avoid Tetris branding in a public release.
- **Original game material.** BlockPlanet runs the original code and needs the player's own ROM. New art, music support and modes are fine; redistributing original assets is not.
- **Determinism.** Ghosts, replays and lockstep online play need the game to replay exactly the same way from the same inputs. The current runtime is not proven deterministic. Seeds and attack messages are safer foundations.
- **Engine limits.** The original game logic has one player per sphere, one cursor, and a fixed draw patch. Cooperative works around this with fixed roles; letting both players drop, or a full free camera, may need replacing parts of the game rather than hooking them.
- **Photosensitivity.** Audio-reactive visuals need flash limits and testing before release.

## Suggested order

1. Visual identity foundations: new UI layer, typography, HUD art, calm and full background modes.
2. Camera framing for all aspect ratios, then the extended sphere patch.
3. Story structure: chapter map, unlocks, per-chapter resources.
4. Media Player, from the jukebox and visualiser research.
5. Time Trial records and seeds, then the daily challenge.
6. Versus formats and the junk system, local first, then online.
7. Cooperative: local first, online last.

## Open questions

- Which existing characters, if any, survive as abstract avatars for VS and custom characters?
- How many chapters should Story have? The original's real chapter count is still being measured.
- Should Hide & Seek stay as its own mode or become a Story level type?
- What reward does finishing Story give: Media Player, Lines, Custom, or all three?
- Cooperative: which support actions feel best in play (magic aiming, hold or swap piece, others)? Needs a playable prototype.
- Should online leaderboards be friends-only, matching the friend-code decision?
