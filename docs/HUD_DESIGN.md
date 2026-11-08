# HUD design (BlockPlanet)

The new in-game HUD, decided with the project owner on 2026-11-05. It replaces the original HUD ([HUD_INVENTORY.md](HUD_INVENTORY.md) lists what the original shows and where). Art is the owner's; this document is the layout and behaviour.

## Principles

- **Show only what matters to the mode.** Each mode has its own set of elements; nothing is shown just because the original showed it.
- **Less on screen.** The original takes too much space and splits attention between the two edges. Elements are compact and grouped.
- **Priority 1 on the left, priority 2 on the right, in every mode.** Priority 1 is what the player needs mid-play (lives, the clock when it ends the game, magic, next pieces); priority 2 is useful but not essential (mode and stage, score, time, progress).
- **Feedback where it happens.** Points, time gained and combo names float up from the spot on the sphere where they were earned, like damage numbers in RPGs, instead of appearing beside a counter. BlockPlanet draws the sphere, so it knows where each board cell is on screen.
- **Fits every screen shape.** Wide screens: priority 1 in a column on the left, priority 2 on the right. Square-ish screens (about 4:3 down to 1:1): the same columns, keeping at least a minimum gap between the sphere and the HUD. Portrait screens (taller than wide): the columns become rows, **priority 1 along the top and priority 2 along the bottom**.
- **Containers** (decided 2026-11-06): one per side, tinted in the menus' colours with round corners and **no border**, its groups separated by thin dividers (left: hearts and drop timer | magic | next pieces; right: mode and stage | time | progress). Each is **centred vertically**. The magic group leaves the item box clear, so the magic's 3D model (drawn by the game under the HUD) shows through.
- **Section titles** (decided 2026-11-06): each section has a small title for new and returning players: Health, Magic, Next on the left; the mode's name (above the stage), Time, Progress on the right (Score in score modes). Sections are spaced a little apart within the container.
- **Portrait rows:** the magic meter is a column beside the item box (filling upwards), so the row stays thin.
- **Placement** (decided 2026-11-06), a player choice in **Graphics > HUD panels**, saved in the profile:
  - **Near the sphere** (the default): where they would be on a 4:3 screen (3:4 on portrait screens, as rows above and below), so on wider or taller screens they stay near the sphere. Still: they do not move with the camera.
  - **Screen edges:** at the screen's edges.
  - **Follow the sphere:** near, and pushed out as the sphere grows on screen (deeper stages, the drop-timer zoom) to keep clear of it, but never past the screen's edge. The sphere's size on screen is estimated from its highest occupied layer and the camera distance (player context `+0x2420`).
  Still is the default because the magic's model and its tint are drawn by the game a frame behind the HUD, so a moving box would trail slightly.
- **The item box can move** (other modes, other layouts): the magic's model follows the box wherever the HUD's layout puts it.
- **Messages and cards** use the menus' style (panels, the original's colours).
- **The sphere** keeps the original's framing and the drop-timer zoom (see [ROADMAP.md](ROADMAP.md#decisions)): the camera rests at the same distance at every stage (about 2,424 at both 1:1 and 10:1, confirmed), so the sphere's size comes from the stage's layers, about 55% of the narrow side at 1:1 and larger than the screen at 10:1. Deep stages and the timer zoom may put the sphere under the HUD or off screen, as in the original; the HUD stays readable over it (a subtle backing where needed). In deep stages the timer zoom starts within seconds.

## Story (Rescue)

| Element | Where | Look and behaviour | Notes |
| --- | --- | --- | --- |
| Hearts | Left, top of the left group | As the original: three hearts, each lost one turns to a skull | |
| Drop timer | Left, directly below the hearts (decided 2026-11-06) | A bar for the time left to drop the held piece; turns red when short. Running out costs a heart | Replaces the orb in a brass casing above the bowl. Player context `+0x2B10` (0 to 600) |
| Magic | Left, below the hearts | An **item box** (a square, like Mario Kart's) holding the stored magic, with a **charge bar** (like a health bar) for the next magic: the **magic meter** (player context `+0x2AEC` of `+0x2AEE`, 14 steps; +1 per combo of 6 or more, +3 per fuse or gravity combo; full gives or upgrades magic). A 20+ combo gives magic straight away, which the bar could celebrate. Losing a life empties the box. The box shows the held magic (`+0x702`: none, or one of six types; another 20+ combo upgrades it to the next type, so the box can show an upgrade) | Replaces the bowl, ring and dot trail. One item, as the original (a second slot is a later idea, perhaps for casual mode) |
| Next pieces | Left, below the magic | **2D sprites** instead of 3D models; the next piece at the top, a little larger; the stack **slides up** as a piece is played | Queue found (player context `+0x2374`, eight pieces ahead; see the research notes). Art: one sprite per shape ID, coloured by the shape's material, plus the "?" Random Block |
| Mode and stage | Top right | Plain text: the mode name small ("Rescue"), the stage number larger beneath ("1:3") | Replaces the RESCUE label and the gold ring |
| Episode timer and splits | Right, middle | **LiveSplit style** (built 2026-11-06): the episode's total game time (play only; paused and between levels not counted), with the **net difference so far** beneath (green ahead, red behind), then a row per stage: stage, difference, time; the current stage ticks. First time through: times only. Each stage is compared with **that stage's best time** (best segments), as it was when the run started; the net is the sum of the completed stages' differences. A new chapter, Try Again or a new game starts a new run. **Single stages (2026-11-06):** Story > a chapter opens a list: Whole episode, then each stage completed and the next one, with its best time. Playing one stage shows **Stage time** (live), **the difference against the best** and **Best** (or "No best yet" / "First time"); when it is completed the time is saved and the game returns to the menu. **Any finished Story stage counts towards that stage's best**, however the run started (a full episode, part of a chapter, or one stage from level select: time-trialling a single stage); only the whole-episode best needs a run from the first stage. Other modes keep their own records. Up to six rows; on portrait screens the total and net, the last completed stage and the current one | Bests per stage and the best whole-episode time saved in the profile (`progress.episode_records`). Levels are generated randomly, so times vary with the board |
| Progress bar | Right, **beneath the stage number** (moved 2026-11-06), in the same group | A % bar. At the level start, the **weakest point** is the core-sized window with the fewest blocks above the core; that count is 100%. Progress = 1 − (blocks left at the current weakest point ÷ the starting count). It does not show where that point is | The opening is S × S core cells at even corners (S = player context `+0x23E4`, 4 to 8 in Rescue); see the research notes. A **scanner** magic that shows the weakest point is a separate idea (ROADMAP) |
| Score, X count, per-move points | Not shown | In the pause menu, the level results and the records | Score-based modes show them (below) |

**Built (2026-11-06), placeholder art:** BlockPlanet draws this HUD in Story play and hides the original's 2D parts, piece queue, bowl and timer orb (`BLOCKPLANET_ORIGINAL_HUD=1` keeps the original for comparison). Hearts, drop timer, magic item box (the magic's name) and meter, the next three pieces (the bar, square, S and T drawn as their shapes; the two shapes not yet seen, probably the Ls, as their cell count), mode and stage, the episode's time and the progress bar. On portrait screens the columns become a row along the top and one along the bottom. Floating numbers (2026-11-06; **off in Story**, decided the same day: Story needs no score pop-ups; built for the score modes, `BLOCKPLANET_POPUPS=1` shows them for testing): when a combo finishes, its points ("+60,000") and block count rise and fade from where the blocks cleared; waves of the same chain join one number. Placed from the cleared cells' offset from where the last piece landed (the front of the sphere), along the sphere's curve; approximate. The game also floats its own number over the sphere: a large grey 3D number ("6000" for a combo of about 59,000 points, so not the total; its meaning is still to find) at the centre, for about a second. **Decision:** no score pop-up in Story, so this is to be hidden there; where a mode needs floating numbers, reuse the game's own mechanic if it can show what is needed, rather than BlockPlanet's. Not yet: and the owner's art. **The item box shows the magic's own 3D model** (decided 2026-11-06), turning slowly with its animation held still: the game's held-magic model is moved into the box, sized to fit, for all six magics. When used, the flying model starts from the box and eases onto the original's path over the sphere, then works the sphere as in the original (checked with the Magnet in a recording). The flight keeps the original's curve and timing: only an offset is added, starting at the box and easing out over 0.6 s. On portrait screens the box is outside the game's picture, so it shows the magic's name instead. The Atom sits a little off-centre in the box (its model's centre differs); per-model adjustment is possible later.

## Messages and cards

| Kind | Look and behaviour |
| --- | --- |
| Level-start card (e.g. Hide & Seek's MULTI, DRILL; mode rules for Lines and VS) | A centred panel: title and one or two lines. Goes when play starts or on a button press |
| Hint (e.g. "Uncover multiple cores for a huge bonus") | A small banner that slides in briefly, at the top |
| Episode card (between episodes) | A full panel: "Episode 2" large, the message beneath (texts in [STORY_PROGRESSION.md](STORY_PROGRESSION.md)) |
| Training instructor text | To be decided (paged lessons) |

## Time Attack and Score Attack (Story's episodes)

**Decided 2026-11-06:** competition is on **Story's episodes only**. The original's Time Trial (a five-minute clock over its own set of about 20 stages) is no longer offered. Solo lists **Story**, **Time Attack** and **Score Attack**; each opens the chapter list (the attacks without Chapter 0, and only chapters reached in Story), then **Whole episode** or a **single stage**. All three run the game's Story mode; BlockPlanet picks the HUD and the records.

- **Story:** the episode's time only (no ± or splits). A finished stage still counts towards Story's stage bests (`progress.episode_records.rescue`).
- **Time Attack:** LiveSplit-style timing: the episode's time, the net ± so far against the best stage times, a row per stage (time and ±; green ahead). A single stage shows its time, the best and ±, then returns to the menu. Records in `progress.episode_records.time_attack`.
- **Score Attack:** Story's stages and clear condition, with a **clock** per stage: **2:00 for a one-layer sphere and 10 seconds more for each further layer** (decided 2026-11-06; Story's spheres have 1 to 6 layers, so 2:00 to 2:50), so games stay short without punishing thicker spheres. The layers come from the level structure (`0x8013D17C`) as the stage starts. Combos add time, less the longer the stage has been played (decided 2026-11-06), so a stage cannot be stretched forever:

  | Play time in the stage | Share of a combo's time |
  | --- | --- |
  | 0:00 to 0:59 | all |
  | 1:00 to 1:59 | two thirds |
  | 2:00 to 2:59 | a quarter |
  | 3:00 on | none |

  A combo's full time is a second a block past two, up to ten (provisional; the original's rule is still to find). The table is `AttackClock::combo_scale` in `src/platform/story_hud.cpp`. When a stage is cleared, its whole minutes left carry to the next stage (in a whole episode) and the remaining seconds become bonus points (1,000 a second, added to the game's score and to that stage's split); a single stage or the episode's last stage turns all its time into points. At 0:00 the run ends with the game's own game over. HUD: "Time left" under Health (red and pulsing in the last 30 seconds); on the right the same template as Time Attack by points: points in the run, the net ± against the best points of each stage (more is better, so green when ahead), a row per stage. A single stage shows its points, the best and ±. Points count from the score the run starts with, since Story's score runs on. Best points per stage in `progress.episode_records.score_attack` (stored where times are, as numbers). A whole episode returns to the menu when it is done.

**Trophies (decided 2026-11-06)**, since stages vary and a fixed score target would be unfair: Score Attack: **Overtime** (play two minutes past the stage's own time in one stage, which takes the time combos add; to tune), **Point Hunter** (a whole episode), **Last Stand** (the final episode, 10, the second bonus episode), **Out-Scored the Maker** (beat the developer's points on every stage). Time Attack: **Speedrunner** (a whole episode), **Photo Finish** (the final episode), **Out-Ran the Maker** (beat the developer's time on every stage). Names are placeholders for the owner. An episode counts when its last stage is cleared.

**Developer records** (`assets/trophies/developer_records.json`, empty until recorded): a time and a score per stage of the 10 episodes. To record them, run the game with `BLOCKPLANET_RECORD_DEVELOPER=<path to a file>` (for example the repository's `assets/trophies/developer_records.json`) and play Time Attack and Score Attack: each finished stage is written to the file when it is the first or better (log: `developer_record`). "Beat" means better or equal. The trophies only unlock once every stage of the mode has a record. Nothing in the file comes from the game.

The earlier Score Attack build on the original Time Trial (its own stages and clear condition, "+N s" pop-ups, best score and longest time survived) is removed; old profiles keep their `progress.score_attack` values, unused. ("Furthest stage reached" is kept as an idea for the Multiphasic mode.)

## Puzzle

The 100 puzzles are fixed designs (copied from stored data, the same every time), so personal bests per puzzle are fully fair.

**Built (2026-11-06):** left, **Drops left** and **Slides left** (context `+0x25C6`, `+0x25C8`, found by comparing memory before and after a drop); right, "Puzzle", the number, and the time (the game's own clock, `+0x2B0C`, counting up in Puzzle) with ± against the best for that puzzle and "Best 0:42 · 2 moves" (the fewest drops and slides it was solved in). Bests in `progress.episode_records.puzzle` and `puzzle_moves` (chapter key 0, index = puzzle − 1). **Open:** in Puzzle with the original HUD hidden, the picture above about y 191 (of 240) is dimmed and the strip below is not, so there is a visible edge. With the original HUD the whole picture is dimmed evenly. Not the queue routine and not the HUD routine's 2D draws tried so far (`BLOCKPLANET_HUD_SKIP_LOG=1` lists them: images at 0x8003396C, the "1x" badge at 0x80036FE4, boxes at 0x80071DE8 including one at (255, 191), digits at 0x8007240C); next, try keeping each of the remaining ones in turn.

| Priority 1 (left, top to bottom) | Priority 2 (right) |
| --- | --- |
| **Drops left** | Puzzle number (text, top right) |
| **Slides left** | **Timer**, with −/+ against the profile's best time for this puzzle |
| | **Best moves** for this puzzle (fewest drops + slides used), e.g. "Best: 4 moves" |

Not shown: score and multiplier. No hearts, piece queue or magic in Puzzle.

## Hide & Seek

Each level has a goal type (MULTI, DRILL, SHIFT, UNIQUE, BRICK, CONNECT, TOWER, CRYSTAL TOWER), told on a card at the start; 30 levels per stage.

| Priority 1 (left, top to bottom) | Priority 2 (right) |
| --- | --- |
| Hearts | Mode, stage and **goal type** (text, top right; e.g. "Hide & Seek" / "1:3" / "DRILL") |
| Magic item box and charge bar | Timer, with −/+ against the profile's best for the stage |
| Next pieces | |

**Built (2026-11-06):** the Story HUD's left side (health and drop timer, magic, next); on the right "Hide & Seek", the level number (its own chapter is not found yet), **Goal** in BlockPlanet's own words (Find any picture, Under a drill, Open the core, Four parts, One shape, Drop a brick, Connect, Around the tower, Crystal tower), and the level's time with ± against the profile's best for that level (`progress.episode_records.hide_and_seek`, chapter key 0 until the chapter is found). To check: the time also runs while the goal card is up at the level's start.

**No progress bar in Hide & Seek** (decided 2026-11-06). Finding the pictures is the mode's challenge, and any bar that moves as pictures are uncovered hints where they are, which purists would not want. The goal type stays on screen as text, which is new compared with the original (it only states the goal on the card at the start).

The progress rules worked out earlier are kept here in case a mode or an assist option wants them later: blocks left above what must be uncovered ÷ blocks there at the start (all the pictures for most goals; the picture with the fewest blocks left for MULTI, whose pictures are the core's 3 × 3 tiles); for BRICK, layers the furthest-fallen brick has dropped ÷ the two layers to the core. Where each goal's pictures, drills, towers and bricks are: [research notes](research/blockplanet-game-state.md#hide--seek-goals-and-their-pictures-2026-11-05).

## Lines

Pieces are removed by lining up three in a row (horizontal or vertical); the goal is a big enough opening in the core. 10 levels per stage. The same layout as Story:

**Built (2026-11-06):** as Story, without Next (Lines shows no queue in the original); on the right "Lines", the stage, the progress bar and the stage's time with ± against the best (`progress.episode_records.lines`). Magic is kept (the original shows the bowl in Lines).

**The game's cards** (Lines' rules at the start, Hide & Seek's goal card) are drawn by its HUD routine. While one is up (`0x801033AC` ≠ 0, found by comparing memory with the card up and gone) BlockPlanet shows the original HUD and not its own, so the card is not hidden.

| Priority 1 (left, top to bottom) | Priority 2 (right) |
| --- | --- |
| Hearts | Mode and stage (text, top right) |
| Magic item box and charge bar (if Lines uses magic; to be checked) | Progress bar toward the core opening |
| Next pieces | Timer, with −/+ against the profile's best for the stage |

## VS (local split screen, and VS CPU)

Each player sees the other's HUD, and each has half the screen, so VS shows **only the necessary items**, in **a single column per player** at their half's outer edge (P1 far left, P2 far right). On **tall screens** the screen splits **horizontally** (P1 above, P2 below) and each HUD is **a single row** along that half's outer edge.

Per player, in order:

1. Hearts / rounds
2. **X count** (attack strength: it sets how many pieces are sent)
3. Magic item box and charge bar
4. Next pieces (the next one or two)
5. Incoming junk marker, only while junk is on its way

**Shared:** a single **progress bar** along the divider shows both players' progress towards their core area and who is closer (each player's colour fills from their end, like a tug of war); on tall screens it runs along the horizontal split.

Not shown in VS: mode and stage text, timer, score.

**Built, first version (2026-11-06), VS CPU:** a slim column per player at the outer edges (rows above and below on tall screens): "Player 1" / "Player 2", hearts with the drop timer, **Attack** (the X count, `+0x2AE6` + `+0x2AE8` + 1, the same value the original shows as "1x"), Magic (the held magic's name and the meter; the 3D models are not placed in VS yet) and the next two pieces. Player 2's values are at the same offsets in its context (`0x80113488`). **Still to do:** the shared tug-of-war progress bar (player 2's board or exposed-core map is not found yet: in VS `0x800E44C8` and `0x800E44CC` point at two 32-word maps, both empty early in a round, so possibly one per player), the incoming junk marker (the original's purple balls between the spheres still show, drawn as models), and the columns overlap the large VS spheres on 16:9 (tinted, so the sphere shows through).

## Other modes (to be decided)

- **Score-based modes** (Score Attack, Multiphasic): score on the right, middle; X count with it. Points from each move and combo float up from where the blocks broke.
- Online VS, Cooperative, Custom: when those modes are designed.

## Research needed

- **The drop timer needs a place in the new HUD** (found 2026-11-05: the orb above the bowl is the drop timer, not magic). It is priority 1 (running out costs a heart). To decide with the owner.

- ~~The piece queue (for the sprites).~~ Found: [research notes](research/blockplanet-game-state.md#the-piece-queue-2026-11-05). Still open: the picture for each shape ID beyond the bar (1) and the square (2).
- ~~The core size each level needs~~ Found: an S × S square of uncovered core, S from the level data ([research notes](research/blockplanet-game-state.md#the-core-opening-a-level-needs-2026-11-05)). Confirmed in play (a won level). To check: whether type 1 blocks can be cleared.
- ~~The magic charge and the stored magic type~~ Found: held magic `+0x702` (−1 none, 0–5 six types, upgraded by each 20+ combo), magic meter `+0x2AEC` of `+0x2AEE`, combo counter `+0x2AE0` ([research notes](research/blockplanet-game-state.md#magic-combos-and-score-2026-11-05)). Art: six item icons. Still to record: what each magic does.
- ~~Where a combo or drop happened on the board, and the time and points each one earned~~ Found: cleared cells from comparing the board each frame, points from the score, time from Score Attack's clock (`+0x2B0C`), the cursor at `+0x2B2A`/`+0x2B2E` ([research notes](research/blockplanet-game-state.md#where-things-happen-on-the-board-for-floating-numbers-2026-11-05)). Still open: telling fuse from gravity combos for their names.
- Hiding the original HUD's 2D elements (done for the pause screen; the same technique) and the 3D piece queue and bowl.
