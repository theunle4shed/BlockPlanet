# Training redesign (proposal, 2026-11-07)

From the owner's notes after seeing Training with BlockPlanet's speech panel: there is too much on screen for a new player; HUD elements should be in Story's style, at the bottom, and shown only once introduced; the N64 pad picture should go (done); the bubble should be narrower and fit its text (done); pages that only show more text should be combined and scrolled; the heading needs weight (done).

## What the lessons are

The game has three lessons, each a few short levels with Wheels' text. Pages are separated by codes in the text (`docs/research/card-text-audit.md`). `-2` is a page break, waiting for A. `-5` is a demonstration ("Watch"): the game plays a move, then carries on. `-3` and `-4` start a scripted action on that page, such as an arrow or Wheels animating.

| Lesson | Levels (one card each) | Teaches |
|---|---|---|
| Basics | Training, Lives, Combos, Magic | the shadow, moving, sliding, removing pieces, lives, the core picture, combos, power pieces, magic |
| Advanced | Crystal pieces, X count, Fuse combos | crystal pieces, the X count, gravity combos, fuse combos |
| Puzzle | one | zoom (L, R), restart (C-up), the wildcard piece, drops and slides |

## Proposal

1. **Layout for wide screens.** The sphere is larger and round, using the true aspect for the Training camera (research: the game's camera squeezes it). It sits left of centre. Wheels stands bottom right, with the panel above him and the tail pointing at him. The HUD runs along the bottom in Story's style.
2. **The HUD grows with the lessons.** Story's HUD is used, but each element appears only when Wheels introduces it, with a short highlight (a glow) the first time:

   | Element | When it appears |
   |---|---|
   | Next pieces | The shadow page |
   | Hearts, with the drop timer | Lives |
   | Progress bar to the core | Game objective |
   | Combo counter | Combos |
   | Magic box | Magic |
   | X count | Advanced |
   | Drops and slides | Puzzle |

   The original HUD (hearts, 1x, the magic bowl, the next piece in the corner) is hidden in Training, as it is in Story.
3. **Pages combined.** Pages that only show more text (after a `-2` break, opening with no `-3` action) are joined into one panel. If the text is too tall, it scrolls with the right stick, Page Up / Page Down or the mouse wheel. A then moves past all of them at once (BlockPlanet presses A for each joined page). Pages with a demonstration or a scripted action stay separate, as the game needs them in turn. The first page ("Press down or the A button to page through the training text") is dropped: the panel's "A More" hint says the same.
4. **New steps where the game says nothing.** BlockPlanet adds its own text pages for what the HUD shows but the lessons never explain: the drop timer, the next pieces, the progress bar, Time and Score Attack's clock. These pages pause the lesson (held on the current page) and highlight the element. They cannot demonstrate moves: the game's demonstrations are its own code.
5. **Shorter lessons in the list.** Training's list could show each level of a lesson as its own step (Moving and sliding, Lives, Combos, Magic; Crystal pieces, X count, Fuse combos), each marked done. This needs research into starting Training at a later level.
6. **Wheels.** He is already the game's 3D model (drawn as a scene each frame, in his training hat), not a picture. He could be larger, placed bottom right as the speaker, and turn or nod as each page appears, using his own scene's animation.

## Done (2026-11-07)

- **Step 1:** the HUD row at the bottom, revealing parts as introduced (`update_training_hud` in `linux_main.cpp`; the row is `HudView::bottom_row`). The thresholds are the addresses of the text that introduces each part (Basics: the current piece `0x800EE8FC`, Lives `0x800EEC5C`, Game objective `0x800EED70`, Magic `0x800EF198`; Advanced: X count `0x800EF44C`; Puzzle: slides and drops `0x800EFAAB`). The combo counter is not a HUD part: the game shows it on the sphere.
- **Step 2:** joined pages (`training_pages`, `training_group`, `update_training_presses`). A page joins the one before it when that one ends with `-2` and it opens with no `-3`. The joined text is read from memory, not typed in.

Open questions from step 2: a long group scrolls, and A still moves on even when the player has not scrolled to its end. A could scroll first (to its end), then move on.

## Suggested order

| Step | Size |
|---|---|
| 1. Hide the original HUD in Training and show Story's HUD at the bottom, revealing elements as above | Medium |
| 2. Combine text-only pages, with scrolling | Medium |
| 3. BlockPlanet's own pages for the HUD elements the lessons skip | Medium |
| 4. The wide-screen layout, with the true camera aspect | Research, then medium |
| 5. Lesson steps in the list, and Wheels animating | Research |

## Research: Training's squashed sphere (2026-11-07)

The game builds every projection with one routine, `guPerspective` at `0x800D47E0`. It is called from six places (`0x80031B58`, `0x8003926C`, `0x8003B5F4`, `0x800C9318`, `0x800CD478`, `0x800CFEC8`), and each passes an aspect of 4:3 (`0x3FAAAAAB`). The narrow sphere in Training does not come from the projection. It must come from the viewport, or from a scale on the sphere's model in Training (wider than tall on screen means a smaller x scale).

Checked further the same day:

- **Not a scale:** the sphere's per-axis scale (player context `+0x2478` flag, `+0x247A` to `+0x247E`) is 1:1 on every axis, and switched off, in Training.
- **The viewport moves it, without squeezing:** Training's viewport (at `0x800E92C8` in memory) is full size (640 × 480 in quarter pixels) but centred at x 377 instead of 640, so the sphere sits left of centre.
- **The real cause is clipping:** with Graphics > Picture set to 4:3, the sphere is about as wide as it is tall (about 408 × 420 pixels at 720p), but its right side is cut off in a straight vertical line at the middle of the screen. The cut makes it read as narrow. The game clips it there to keep the sphere clear of the original speech bubble. It does this with BlockPlanet's sphere off too.
- **Where the clip is set:** probably one of the game's scissor commands (`G_SETSCISSOR`, `0xED`), set in `func_8006FA28`, `func_800A43E0`, `func_800A4D60`, `func_800C25FC`, `func_800CB070` and `func_800CD3B8`. All of them cover the full height. `func_8006FA28`'s covers the full width (320); the others' widths are computed and not yet read.

**Found and fixed:** a memory dump in Training shows the game's table of ready-made viewports and scissors around `0x80165000`. It holds scissors for the whole screen and for each half (x 0 to 160, x 160 to 320; VS's split screen). Training draws its sphere through the left-half scissor. In Training, BlockPlanet widens that scissor to the whole screen, and puts it back for every other mode (`update_training_scissor` in `linux_main.cpp`). The sphere is now whole and round. Switch `BLOCKPLANET_ORIGINAL_TRAINING_CLIP=1` keeps the original clip.

## Research: Training's sphere loses bricks (2026-11-08)

The owner's photos of Training on real hardware show a complete pink surface round the pit, with walls that join at the corners. In BlockPlanet the surface shows stripes and broken edges. Measured:

- The lesson's board loads complete: every cell of the brick layer (layer 1 of the grid at `0x80116BF0`, value `0x1A7`) is filled, except the pit.
- After play starts, single cells across the sphere are cleared, one by one, seconds apart (for example 41, 48, 52 and 61 seconds into a run), while the lesson waits on its first pages with no input. By the time the screenshots are taken, the band of columns above and below the pit has lost alternate cells: the stripes.
- It happens with BlockPlanet's text, HUD, sphere drawing and Training clip all off (`BLOCKPLANET_ORIGINAL_TEXT=1`, `BLOCKPLANET_ORIGINAL_HUD=1`, `BLOCKPLANET_SPHERE=original`, `BLOCKPLANET_ORIGINAL_TRAINING_CLIP=1`), and without BlockPlanet's A presses. It is not BlockPlanet's presentation.
- Story is not affected: watched cells stayed the same for 45 seconds without input.

So something in the game as it runs here clears Training's bricks, which real hardware does not. Suspects, in order:
1. A difference in timing (Training's demonstrations follow scripted inputs and timers).
2. 8 MB of memory instead of 4 (the game may choose a different layout).
3. A recompilation fault in a Training-only routine.

Next: find the routine that writes the zeros, by trapping writes to one cell in the recompiled code. Until then, the drawing fills single-cell gaps outside the pit (`sphere_cell.cpp`), which hides the stripes but not the cause. The watch log (`BLOCKPLANET_WATCH`) now prints the time of each change.
