# HUD design (BlockPlanet)

The new in-game HUD, decided with the project owner on 2026-11-05. It replaces the original HUD ([HUD_INVENTORY.md](HUD_INVENTORY.md) lists what the original shows and where). Art is the owner's; this document is the layout and behaviour.

## Principles

- **Show only what matters to the mode.** Each mode has its own set of elements; nothing is shown just because the original showed it.
- **Less on screen.** The original takes too much space and splits attention between the two edges. Elements are compact and grouped.
- **Priority 1 on the left, priority 2 on the right, in every mode.** Priority 1 is what the player needs mid-play (lives, the clock when it ends the game, magic, next pieces); priority 2 is useful but not essential (mode and stage, score, time, progress).
- **Feedback where it happens.** Points, time gained and combo names float up from the spot on the sphere where they were earned, like damage numbers in RPGs, instead of appearing beside a counter. BlockPlanet draws the sphere, so it knows where each board cell is on screen.
- **Messages and cards** use the menus' style (panels, the original's colours).
- **The sphere** keeps the original's framing and the drop-timer zoom (see [ROADMAP.md](ROADMAP.md#decisions)).

## Story (Rescue)

| Element | Where | Look and behaviour | Notes |
| --- | --- | --- | --- |
| Hearts | Left, top of the left group | As the original: three hearts, each lost one turns to a skull | |
| Magic | Left, below the hearts | An **item box** (a square, like Mario Kart's) holding the stored magic, with a **charge bar** (like a health bar) for the next magic | Replaces the bowl, ring and dot trail. One item, as the original (a second slot is a later idea, perhaps for casual mode) |
| Next pieces | Left, below the magic | **2D sprites** instead of 3D models; the next piece at the top, a little larger; the stack **slides up** as a piece is played | Needs the piece-queue data (research) and art per piece shape |
| Mode and stage | Top right | Plain text: the mode name small ("Rescue"), the stage number larger beneath ("1:3") | Replaces the RESCUE label and the gold ring |
| Episode timer | Right, middle | The episode's total time, with the difference from the profile's personal best for that point of the episode (**−5:21** ahead, **+2:11** behind) | Personal bests per episode in the records file. Levels are generated randomly, so per-episode times are the fair comparison |
| Progress bar | Right, with the timer | A % bar. At the level start, the **weakest point** is the core-sized window with the fewest blocks above the core; that count is 100%. Progress = 1 − (blocks left at the current weakest point ÷ the starting count). It does not show where that point is | Needs the core size each level requires (research). A **scanner** magic that shows the weakest point is a separate idea (ROADMAP) |
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

## Other modes (to be decided)

- **Score-based modes** (Score Attack, Multiphasic): score on the right, middle; X count with it. Points from each move and combo float up from where the blocks broke.
- **VS:** the X count matters (it sets how many pieces are sent), so it is shown clearly.
- **Hide & Seek**, **Lines**, **VS**: next in the review.

## Research needed

- The piece queue (for the sprites).
- The core size each level needs, and where the game counts it (for the progress bar).
- The magic charge and the stored magic type (for the item box and bar).
- Where a combo or drop happened on the board (for the floating numbers), and the time and points each one earned.
- Hiding the original HUD's 2D elements (done for the pause screen; the same technique) and the 3D piece queue and bowl.
