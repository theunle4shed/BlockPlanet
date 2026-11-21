# Training redesign

## The fresh approach (decided 2026-11-20)

The owner's decision, after reading the script review (`docs/research/training-script-review.md`): the original lessons are not patched further. Their short pages (a couple of sentences, press A, a couple more), the tutor and the scripted steps come from the original system's limits. BlockPlanet builds its own lessons from scratch, laid out by what Rescue asks of the player. The original lessons stay playable, but stop being the main path. The proposal below (2026-11-07) is kept as the record of what was done to the original lessons.

**Every lesson follows one pattern:**

1. **Show.** A short demonstration plays by itself on a prepared board, showing which button is pressed and when. Start skips it.
2. **Reset.** The board goes back to how the demonstration started.
3. **Your turn.** The player does the same thing once, on a board set up so they can succeed.
4. **Check.** BlockPlanet sees the goal reached (from the game's counters: bricks broken, combo size, fuse and gravity counts, hearts, magic used) and shows "<Lesson> Complete": Next, Try Again, Back to Training. If the goal is missed (a heart lost, the magic wasted), the board is set up again with a short hint.

One idea per lesson, under a minute, one or two sentences of text (no paging), no tutor, and "lesson" rather than "level". **Every lesson opens with its purpose (the owner, 2026-11-20):** a line on why it matters, what problem it solves, before how to do it, on its own page ([A] Next), then how, with the choice to try or watch (Slide: "To clear pieces, you need at least two matching pieces together, then drop a third onto them.").

**The lessons, by what the player needs and when:**

| Group | Lesson | Show | Your turn | What the player is told (draft) |
| --- | --- | --- | --- | --- |
| The Basics | The Goal | The sphere is dug down to the picture | (none) | Clear the bricks over the core to uncover the picture. |
| | Drop | The shadow moves onto a matching pair; A drops a third; all three break | Drop onto a pair the shadow is already over, then find one more | Your shadow is your next piece. Drop it onto two matching pieces to break all three. |
| | Slide | B held on a piece, slid beside its twin, then dropped on | Line up a pair by sliding, then drop | Hold B on a piece to slide it. Line up a pair, then drop on it. |
| | Hearts and the Drop Timer | A drop with no match lands on top and costs a heart; the bar runs out and the piece falls by itself | Three good drops before the bar empties | Drop on a match before the bar runs out. A drop with no match costs a heart and leaves rubble to clear. |
| Getting Faster | Combos | More than three pieces break at once; power pieces left behind | Make a combo of four or more | Break more than three at once for a combo. Smaller combos leave power pieces. |
| | Earning Magic | A combo of 20+ earns a magic, which is fired | Make one on a prepared board, then fire it | A combo of 20 or more earns a magic. Press [Magic] to use it. |
| | The six magics | (built: one card, fire it, see what it digs) | | |
| Scoring Big | Power Pieces and Fuse Combos | A flashing power piece is slid into line and starts a new combo | Make a fuse combo | Breaking power pieces can still be slid. Line one up to start a fuse combo. |
| | Gravity Combos | A piece slid out from under another; the top one falls into a match | Make a gravity combo | Slide a piece out from under another; if the top one falls into a match, that's a gravity combo. |
| | X Count | Fuse and gravity combos raise the X count; the payout grows | Reach X3 | Every fuse or gravity combo raises your X count, which multiplies your combo points. |
| | Crystal Bricks | B held over a crystal group turns it into a power piece | Make one and use it | Hold B over crystals to turn them into a power piece. |
| Other Modes | Puzzle Controls, Lines | One demonstration, one go each | | (VS: one line saying every magic acts as a Firecracker there) |

**Hints in play as well as lessons.** In the first Rescue stages, one-line hints at the moment they matter: the first time the drop bar is nearly empty, the first heart lost (and why), the first magic earned.

**What it needs:**

- **Boards:** the research tools already build boards from whole pieces, give magic and read the counters (`research_lab:*`, `magic_lesson`, `docs/research/blockplanet-score-and-magic.md`).
- **Demonstrations:** new. BlockPlanet plays the move itself through the controller (as `research_fuse_auto` does and the test harness does), so the game's own rules apply; a button picture lights as each press is made. The same driver later serves the computer opponent's moves.
- **Reset:** the board saved before the demonstration and written back after it.

**Order:** The Basics first (Drop as the template lesson: show, reset, your turn, check), then the rest in the same pattern.

**Progress:**
- **Slide: built (2026-11-20, the owner's design).** Six squares scattered in an 8 × 8 pit, gathered into one group by sliding; dropping off. Try it or Watch. Reduced to two squares (the owner). **Drop continues on the same board (built 2026-11-20):** "How to Drop", Try it or Show me; the demonstration finds a square wherever the player left the pair. The lesson is now "Slide and Drop".
- **Drop: built (2026-11-20).** `new_lesson:drop` (`drop_lesson_phase` in `src/platform/linux_main.cpp`). The demonstration waits for the card to close (the game is paused under it), then presses one direction every 400 ms, steered by where the cursor is, until the shadow is over the hole; a pause; A. Its presses go through the injected controller with bit 17 set, which copies the D-pad onto the stick (the game steers the shadow by the stick): D-pad right moves the cursor −1 in x, down −1 in y. A landing is judged 1.5 s later (it lands before it scores): scored is done, not scored is a miss. **The board (2026-11-20):** as the original Training: smooth walls, a pit with a white floor (core tile 87, the original's), sized to the lesson (Drop: just round its pieces; long combos will want a larger one). **Layout (2026-11-20):** Training's: the sphere left (Casual's sphere viewport moved to Training's centre, 377), the text panel right with the lesson's name, what it teaches and what is happening now, the Next row under the sphere; no start card (the owner: no intro card for each section). Still to add: Start to skip the demonstration; Next to the following lesson once Slide exists; a button picture lighting with each press.

## Proposal (2026-11-07): the original lessons

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
4. **New steps where the game says nothing.** BlockPlanet adds its own text pages for what the HUD shows but the lessons never explain: the drop timer, the next pieces, the progress bar, Time and Score Attack's clock. Also mechanics the lessons skip: **rubble** (sliding into them, converting a patch into the held brick's kind to set up combos, and that dropping onto them costs a life), the **mystery block** and the **Random Block**. These pages pause the lesson (held on the current page) and highlight the element. They cannot demonstrate moves: the game's demonstrations are its own code.
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

**Found (2026-11-08): the routine that removes the bricks.** Writes to the brick layer were trapped in the recompiled code (a research build only, now removed):

- `func_800CD3B8` (the per-frame sphere routine) calls `func_800CC408` every frame, unless `func_800B9090` says the board is busy. That routine is a board checker. It keeps a cursor in the player context (`+0x325A` x, `+0x325C` y) and checks 5 cells a frame on layers 1 to 7, so it covers the whole sphere about every 7 seconds.
- For each cell, `func_800CC0D0` works out the brick's piece:
  - the piece id is the cell value ÷ 9; Training's filler is `0x1A7`, piece 47;
  - `func_80093214` finds the piece's origin from its shape table at `0x8014CC98`;
  - `func_800924E0` checks that every cell of the shape is still there;
  - if the check fails, it removes the piece's cells with `func_8008CA34`; an empty id is cleared outright.
- In the run that showed stripes, it removed one band cell per row from its first pass: (20, 0), (18, 1), (16, 2) and so on, going back 2 columns a row. Each later pass removed more. That cascade is the stripes.
- Ten later runs with the same script did not reproduce it. Every filler cell was checked, and each found its origin at its own centre and passed. The board loads identically in good and bad runs. So the starting point is still unknown: something occasionally makes a filler cell fail the check, and from then on the checker eats the band. The origin is always set, so it isn't reading leftover stack.

Next, when it shows again: trap `func_800924E0`'s result in `func_800CC0D0` to see which cell fails first, and what is around it. Until then, the drawing fills single-cell gaps outside the pit (`sphere_cell.cpp`), which hides the stripes but not the cause. The watch log (`BLOCKPLANET_WATCH`) prints the time of each change.

## Combos and Magic (draft, 2026-11-21; terms as docs/STYLE.md, Magic words)

Combos and earning a power-up in one lesson (the owner: the magic lesson makes a big combo anyway, so no separate Combos lesson; power pieces and fuse combos stay in Scoring Big).

**Page 1, "Combos and Magic"** (the owner's layout, 2026-11-21): the line "Break more than three pieces at once and you've made a combo: the more you break together, the more rewarding it is." as plain text, then ([A]) three bullets: "• Fill your magic meter faster" / "• Clear the sphere faster" / "• Score more points".

**Page 2, "Earning Power-Ups"** (fresh page; the owner's choice, 2026-11-21): two bullets, "• Combos of 6 or more top up your magic meter: fill it up for a power-up." / "• Break 20 or more at once for an instant power-up." (From the research: 6 to 19 pieces add 1 to the meter, which holds 14, 9 in VS; gravity and fuse combos add 3; 3 to 5 add nothing; 20 or more give a power-up at once, the meter left as it was plus 1. To be confirmed in play, with whether a second power-up upgrades the one held.) Then a new page: "In this lesson you'll make a big combo, earn a power-up, and use it."

**Step 1, topping up the meter** (the owner, 2026-11-21): after page 2, a board in the usual small pit with **three stacked layers**, each a group of 5 matching pieces: **blue squares on top, red Ts under them, yellow bars at the bottom** (the red T added for the lesson as in Hearts). The pieces in hand come in that order (the Next row shows it). Each drop on the top group breaks 6, tops up the meter by one and uncovers the layer below; the panel counts the meter along ("Your magic meter: 12 of 14…"). The meter starts at **11 of 14**, so the third drop fills it and a power-up appears. [A] Try it or [B] Show me. Kept simple on purpose (the owner: don't overwhelm; three colours). Needs: boards of several layers (walls, faces and columns per layer, as the magic lessons' striped board), and a compact group of 5 for each shape from its fitting positions.

**Then the end** (the owner, 2026-11-21): once the meter is full, a screen: "Congratulations, you've unlocked a power-up." [A], then beneath it: "Get a combo of 20+ to unlock one instantly!", and the lesson is done. (No separate 20+ board or using step: using each power-up is taught in its own lesson.)

**Page 2, "How to Earn a Power-Up"**: "A large group of matching pieces is ready. Drop your piece next to it to break the whole group." / "When the combo reaches 20, a power-up appears in your Magic box." / "Press [Magic] to use your power-up where your shadow is." / [A] Try it, [B] Show me.

**Board**: a larger pit with about 20 matching squares and one gap; the HUD shows Next, the combo count and the Magic box. **Show me**: drop into the gap, the count passes 20, the power-up appears, then it is used, with a pause to see it. **Try it**: the same; done once the power-up is earned and used.

**To check first**: whether a 20+ combo always gives the same power-up or a random one; whether smaller combos part-fill the magic meter (a line if so); and the magic lessons' known issues (a second power-up arriving after one is used; the wrong one given).

**Combos and Magic, built (2026-11-21), the owner's layout:** an 8 x 8 pit with the white floor, one wall high, and three separate stacks in it, each one shape piled five high (blue squares, red Ts, yellow bars). A drop on top breaks the stack as one combo going down: five and the dropped piece, six, which tops up the meter (checked in play: x6, the meter +1). The meter starts at 11 of 14, so the three stacks fill it. Sliding is off. The pieces come blue, red, yellow. Done once all three are cleared: the two ending lines, then Complete.

## Rubble and Mystery Blocks (built 2026-11-21, from the owner's idea)

The fourth lesson of The Basics (Mystery Blocks merged in; the owner asked for a name, this is the suggestion). Cell values and what B and sliding do: `docs/research/blockplanet-game-state.md` (Rubble).

**Part 1, pages:** "Rubble and Mystery Blocks": rubble is the single dark blocks with a glowing orb, often in patches between pieces, and never matches anything ([A]) / but you can turn it into pieces, to join groups into bigger combos. "How to Convert Rubble": move the shadow over rubble the same shape as your piece ([A]) / press [B] to turn it into a piece like yours. "Convert the Rubble": three yellow pieces split by two lines of rubble; convert both, then drop to break them all in one combo. [A] Try it, [B] Show me.

**Part 1, board:** a pit with the white floor and a column across the yellow bars: bar, rubble line, bar, rubble line, bar; the shadow starts on the floor beside the middle bar. Converting both lines and dropping makes a combo of 6 (the five pieces and the one dropped; the owner's "combo of 5" counted the pieces on the board). Fewer than 6 (a line not converted): "Only N broke…" and the board is set up again. The panel counts the rubble lines left.

**Part 2, pages:** "Mystery Blocks": it flickers yellow and red, hidden among the pieces and rubble, with a power-up inside ([A]) / free it with a combo right next to it, or by sliding a piece into it. "Find the Power-Up": a mystery block is buried in this rubble; hold [B] on the yellow piece and slide it through the rubble to reach it. Try it, Show me.

**Part 2, board:** a yellow bar with three lines of rubble beyond it, a mystery block in the middle of the middle line. Sliding the bar through frees it; done when a power-up is held. Dropping is off in this part. Then "Well Done": you found a power-up! ([A]) / press [Magic] to use it; the Power-Ups lessons show what each one does. Then "Rubble and Mystery Blocks Complete".

**Every reset lays the board out from where the shadow is** (the board moves, not the camera), three cells along the bars from it, on the side away from the board's seam.

**To decide (the owner):** the name; whether the mystery block should give a set power-up (it gave the Firecracker when none was held); whether Part 2 should also show a combo next to a mystery block freeing it.
