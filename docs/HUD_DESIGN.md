# HUD design (BlockPlanet)

The new in-game HUD, decided with the project owner on 2026-11-05. It replaces the original HUD ([HUD_INVENTORY.md](HUD_INVENTORY.md) lists what the original shows and where). Art is the owner's; this document is the layout and behaviour.

## Principles

- **Show only what matters to the mode.** Each mode has its own set of elements; nothing is shown just because the original showed it.
- **Less on screen.** The original takes too much space and splits attention between the two edges. Elements are compact and grouped.
- **Priority 1 on the left, priority 2 on the right, in every mode.** Priority 1 is what the player needs mid-play (lives, the clock when it ends the game, magic, next pieces); priority 2 is useful but not essential (mode and stage, score, time, progress).
- **Feedback where it happens.** Points, time gained and combo names float up from the spot on the sphere where they were earned, like damage numbers in RPGs, instead of appearing beside a counter. BlockPlanet draws the sphere, so it knows where each board cell is on screen.
- **Fits every screen shape.** Wide screens: priority 1 in a column on the left, priority 2 on the right. Square-ish screens (about 4:3 down to 1:1): the same columns, keeping at least a minimum gap between the sphere and the HUD. Portrait screens (taller than wide): the columns become rows, **priority 1 along the top and priority 2 along the bottom**.
- **Messages and cards** use the menus' style (panels, the original's colours).
- **The sphere** keeps the original's framing and the drop-timer zoom (see [ROADMAP.md](ROADMAP.md#decisions)): the camera rests at the same distance at every stage (about 2,424 at both 1:1 and 10:1, confirmed), so the sphere's size comes from the stage's layers, about 55% of the narrow side at 1:1 and larger than the screen at 10:1. Deep stages and the timer zoom may put the sphere under the HUD or off screen, as in the original; the HUD stays readable over it (a subtle backing where needed). In deep stages the timer zoom starts within seconds.

## Story (Rescue)

| Element | Where | Look and behaviour | Notes |
| --- | --- | --- | --- |
| Hearts | Left, top of the left group | As the original: three hearts, each lost one turns to a skull | |
| Magic | Left, below the hearts | An **item box** (a square, like Mario Kart's) holding the stored magic, with a **charge bar** (like a health bar) for the next magic: the current combo towards the 20 pieces that earn a magic | Replaces the bowl, ring and dot trail. One item, as the original (a second slot is a later idea, perhaps for casual mode) |
| Next pieces | Left, below the magic | **2D sprites** instead of 3D models; the next piece at the top, a little larger; the stack **slides up** as a piece is played | Queue found (player context `+0x2374`, eight pieces ahead; see the research notes). Art: one sprite per shape ID, coloured by the shape's material, plus the "?" piece |
| Mode and stage | Top right | Plain text: the mode name small ("Rescue"), the stage number larger beneath ("1:3") | Replaces the RESCUE label and the gold ring |
| Episode timer | Right, middle | The episode's total time, with the difference from the profile's personal best for that point of the episode (**−5:21** ahead, **+2:11** behind) | Personal bests per episode in the records file. Levels are generated randomly, so per-episode times are the fair comparison |
| Progress bar | Right, with the timer | A % bar. At the level start, the **weakest point** is the core-sized window with the fewest blocks above the core; that count is 100%. Progress = 1 − (blocks left at the current weakest point ÷ the starting count). It does not show where that point is | The opening is S × S core cells at even corners (S = player context `+0x23E4`, 4 to 8 in Rescue); see the research notes. A **scanner** magic that shows the weakest point is a separate idea (ROADMAP) |
| Score, X count, per-move points | Not shown | In the pause menu, the level results and the records | Score-based modes show them (below) |

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
| Magic item box and charge bar | **Goal progress** for the type: e.g. "Drills found 2/5" for DRILL, a % bar like Story's for MULTI |
| Next pieces | Timer, with −/+ against the profile's best for the stage |

New compared with the original, which only states the goal at the start. Each goal type needs research to count its progress.

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
- The magic charge and the stored magic type (for the item box and bar). Partly found: magic comes from a combo of 20+ pieces, and the combo counter is player context `+0x2B14`, so the charge bar is the combo towards 20. The stored magic and its type are still to find ([research notes](research/blockplanet-game-state.md#magic-combos-and-score-2026-11-05-in-progress)).
- Where a combo or drop happened on the board (for the floating numbers), and the time and points each one earned.
- Hiding the original HUD's 2D elements (done for the pause screen; the same technique) and the 3D piece queue and bowl.
