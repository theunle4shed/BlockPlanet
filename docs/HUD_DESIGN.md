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
| Next pieces | Left, below the magic | **2D sprites** instead of 3D models; the next piece at the top, a little larger; the stack **slides up** as a piece is played | Queue found (player context `+0x2374`, eight pieces ahead; see the research notes). Art: one sprite per shape ID, coloured by the shape's material, plus the "?" piece |
| Mode and stage | Top right | Plain text: the mode name small ("Rescue"), the stage number larger beneath ("1:3") | Replaces the RESCUE label and the gold ring |
| Episode timer | Right, middle | The episode's total time, with the difference from the profile's personal best for that point of the episode (**−5:21** ahead, **+2:11** behind) | Personal bests per episode in the records file. Levels are generated randomly, so per-episode times are the fair comparison |
| Progress bar | Right, with the timer | A % bar. At the level start, the **weakest point** is the core-sized window with the fewest blocks above the core; that count is 100%. Progress = 1 − (blocks left at the current weakest point ÷ the starting count). It does not show where that point is | The opening is S × S core cells at even corners (S = player context `+0x23E4`, 4 to 8 in Rescue); see the research notes. A **scanner** magic that shows the weakest point is a separate idea (ROADMAP) |
| Score, X count, per-move points | Not shown | In the pause menu, the level results and the records | Score-based modes show them (below) |

**Built (2026-11-06), placeholder art:** BlockPlanet draws this HUD in Story play and hides the original's 2D parts, piece queue, bowl and timer orb (`BLOCKPLANET_ORIGINAL_HUD=1` keeps the original for comparison). Hearts, drop timer, magic item box (the magic's name) and meter, the next three pieces (the bar, square, S and T drawn as their shapes; the two shapes not yet seen, probably the Ls, as their cell count), mode and stage, the episode's time and the progress bar. On portrait screens the columns become a row along the top and one along the bottom. Floating numbers (2026-11-06): when a combo finishes, its points ("+60,000") and block count rise and fade from where the blocks cleared; waves of the same chain join one number. Placed from the cleared cells' offset from where the last piece landed (the front of the sphere), along the sphere's curve; approximate. The game also floats its own 3D score over the sphere ("7000"); whether to keep it is open. Not yet: the personal-best difference for the episode time, and the owner's art. **The item box shows the magic's own 3D model** (decided 2026-11-06), turning slowly with its animation held still: the game's held-magic model is moved into the box, sized to fit, for all six magics. When used, the flying model starts from the box and eases onto the original's path over the sphere, then works the sphere as in the original (checked with the Magnet in a recording). The flight keeps the original's curve and timing: only an offset is added, starting at the box and easing out over 0.6 s. On portrait screens the box is outside the game's picture, so it shows the magic's name instead. The Atom sits a little off-centre in the box (its model's centre differs); per-model adjustment is possible later.

## Messages and cards

| Kind | Look and behaviour |
| --- | --- |
| Level-start card (e.g. Hide & Seek's MULTI, DRILL; mode rules for Lines and VS) | A centred panel: title and one or two lines. Goes when play starts or on a button press |
| Hint (e.g. "Uncover multiple cores for a huge bonus") | A small banner that slides in briefly, at the top |
| Episode card (between episodes) | A full panel: "Episode 2" large, the message beneath (texts in [STORY_PROGRESSION.md](STORY_PROGRESSION.md)) |
| Training instructor text | To be decided (paged lessons) |

## Time Trial

| Priority 1 (left, top to bottom) | Priority 2 (right) |
| --- | --- |
| Hearts | Mode and stage (text, top right) |
| **Countdown clock**, below the hearts; turns red or pulses in the last 30 seconds | Score |
| Magic item box and charge bar | Progress bar |
| Next pieces | |

**Time gained** ("+5 s") floats up from where it was earned on the sphere, not beside the clock.

## Puzzle

The 100 puzzles are fixed designs (copied from stored data, the same every time), so personal bests per puzzle are fully fair.

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

**No progress bar in Hide & Seek** (decided 2026-11-06). Finding the pictures is the mode's challenge, and any bar that moves as pictures are uncovered hints where they are, which purists would not want. The goal type stays on screen as text, which is new compared with the original (it only states the goal on the card at the start).

The progress rules worked out earlier are kept here in case a mode or an assist option wants them later: blocks left above what must be uncovered ÷ blocks there at the start (all the pictures for most goals; the picture with the fewest blocks left for MULTI, whose pictures are the core's 3 × 3 tiles); for BRICK, layers the furthest-fallen brick has dropped ÷ the two layers to the core. Where each goal's pictures, drills, towers and bricks are: [research notes](research/blockplanet-game-state.md#hide--seek-goals-and-their-pictures-2026-11-05).

## Lines

Pieces are removed by lining up three in a row (horizontal or vertical); the goal is a big enough opening in the core. 10 levels per stage. The same layout as Story:

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

## Other modes (to be decided)

- **Score-based modes** (Score Attack, Multiphasic): score on the right, middle; X count with it. Points from each move and combo float up from where the blocks broke.
- Online VS, Collaborate, Custom: when those modes are designed.

## Research needed

- **The drop timer needs a place in the new HUD** (found 2026-11-05: the orb above the bowl is the drop timer, not magic). It is priority 1 (running out costs a heart). To decide with the owner.

- ~~The piece queue (for the sprites).~~ Found: [research notes](research/blockplanet-game-state.md#the-piece-queue-2026-11-05). Still open: the picture for each shape ID beyond the bar (1) and the square (2).
- ~~The core size each level needs~~ Found: an S × S square of uncovered core, S from the level data ([research notes](research/blockplanet-game-state.md#the-core-opening-a-level-needs-2026-11-05)). Confirmed in play (a won level). To check: whether type 1 blocks can be cleared.
- ~~The magic charge and the stored magic type~~ Found: held magic `+0x702` (−1 none, 0–5 six types, upgraded by each 20+ combo), magic meter `+0x2AEC` of `+0x2AEE`, combo counter `+0x2AE0` ([research notes](research/blockplanet-game-state.md#magic-combos-and-score-2026-11-05)). Art: six item icons. Still to record: what each magic does.
- ~~Where a combo or drop happened on the board, and the time and points each one earned~~ Found: cleared cells from comparing the board each frame, points from the score, time from Time Trial's clock (`+0x2B0C`), the cursor at `+0x2B2A`/`+0x2B2E` ([research notes](research/blockplanet-game-state.md#where-things-happen-on-the-board-for-floating-numbers-2026-11-05)). Still open: telling fuse from gravity combos for their names.
- Hiding the original HUD's 2D elements (done for the pause screen; the same technique) and the 3D piece queue and bowl.
