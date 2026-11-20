# What the game's Training teaches, against what we have found

Reviewed 2026-11-20 (the owner's request: BlockPlanet's own lessons should not repeat the original's, and Training as a whole should cover the basic and advanced play we discover). The lessons' text was read in memory (`research_text`, text addresses in [blockplanet-game-state.md](blockplanet-game-state.md)) and is summarised here in our own words; the text itself is the game's and is not copied into the repository. Only each lesson's opening was read; the rest is to read before a lesson is designed.

## The original's lessons

| Lesson | What it teaches (summary) |
| --- | --- |
| Basics: introduction, lives | The goal and the controls; losing hearts (not reviewed in detail) |
| Basics: combos | Three or more pieces removed together make a combo; the combo counter counts the pieces as they break |
| Basics: magic | A combo of 20 or more gives a magic; its icon; it removes many pieces quickly; the button to use it |
| Advanced: crystal pieces | Crystal pieces lie about the ball; with the shadow over a group of them, B turns them into a power piece in the shadow's shape |
| Advanced: X count | The number in the coloured box multiplies the combo's score (and, in VS, what is sent to the opponent); ways to raise it, starting with gravity combos |
| Advanced: fuse combos | A breaking piece flashes: a "fuse". A fuse from a power combo can be slid; slid into line with two or more pieces, it sets off a new combo, a fuse combo |
| Puzzle | Its own lesson (not reviewed) |

This settles the owner's memory (2026-11-20) of moving a white, flashing block to chain another combo: it is the fuse combo, and the original teaches it. The same lesson says nothing of dropping a piece onto a running combo.

## What is covered and what is not

| Mechanic (our research) | In the original's Training? |
| --- | --- |
| Combos, counted in pieces | Yes (Basics) |
| Magic from a 20+ combo | Yes (Basics), but not what each magic does (the six effects in [blockplanet-score-and-magic.md](blockplanet-score-and-magic.md)) |
| Power pieces from small combos (1 to 19) | To check in the rest of the combos lesson; crystals into power pieces: yes (Advanced) |
| A combo started on a power piece pays more and lasts longer (the steps) | Partly (the fuse lesson's power combo); the scoring itself is not taught |
| Gravity combos | Named in the X count lesson; to check whether it shows one |
| Fuse combos (sliding a flashing piece) | Yes (Advanced) |
| The X count (gravity + fuse + 1) | Yes (Advanced) |
| The drop timer and its fall (what drops a piece, how long each episode allows) | Not seen; to check the lives lesson |
| What costs a heart (a drop that matches nothing; landing at layer 8) | To check the lives lesson |
| Sliding in general (B held, onto free cells, sinking over holes) | Only as part of the crystal and fuse lessons |
| The highlight: light where a drop is allowed, dark where not | Not seen |
| Each magic's area and how to aim it (Magnet and Ray Gun steered; Dynamite's ring; Atom) | No |
| Wildcard ("?") pieces, mystery blocks, rubble | Not seen |

## Proposal

BlockPlanet's labs (the combo lab first, [blockplanet-score-and-magic.md](blockplanet-score-and-magic.md)) test each mechanic; once confirmed, a lab becomes a lesson only where the original has none: **the six magics** (one board each, showing its area and how to aim it), **gravity combos** (one set up to fall into a match), **what costs a heart and the drop timer**, **the drop highlight**, and **power-piece scoring**. Combos, crystals, the X count and fuse combos stay the original's lessons.

## Built: the magic lessons (2026-11-20)

Training lists six BlockPlanet lessons under **Magic** (Firecracker, Dynamite, Magnet, Atom, Bomb, Ray Gun; `magic_lesson:K`), in place of the "The Six Magics" placeholder. Each starts Casual 1:1 on its own, and once in play builds a **striped patch** (the owner's idea): six layers alternating blue 2 × 2 squares and yellow bars in an 18 × 16 patch round the cursor (six bars or nine squares across), the rest of the sphere pink walls (magic does not break walls, so the damage stays in the patch; the owner's idea to use part of the sphere). The magic is given, a card says in BlockPlanet's words what it does and which button fires it, the drop timer is held, and the core square needed to win is raised to 8 × 8 (the largest) so that no magic ends the lesson by opening the core. About 9 s after the magic is used, play is left for the Training list with a panel: Next (the next magic), Try Again, Back to Training. Checked end to end with the Bomb in the harness.

All six checked end to end in the harness (2026-11-20): card, the magic at work on the patch, results. The Magnet and Ray Gun dig to the core, and the bricks round their tunnel fall into gravity combos that score (about 60,000 to 110,000); harmless, and a glimpse of gravity combos. Done (2026-11-20): a magic lesson is marked when its magic is used (profile progress `chapters_completed["magic_lessons"]`, the magics done), and Training's list shows it as "Magic · completed".

**Revised (2026-11-20, the owner's notes):** the card says only what the magic does and the button that fires it; the HUD shows only the magic (its box, with the model turning in it) and "Training · <magic> · Magic left" (hearts, the drop timer, next pieces, time and score hidden; the HUD's main layout now respects `show_lives`); the results title is "<Magic> Complete". A magic lost without a blast (no bricks broken 6 s after it went, for example lost with a heart) is given back; leaving play before using it ends the lesson (before, the lesson stayed armed and the next Casual 1:1 kept its drop timer held). Not done: the model inside the card (while the card is up the game is paused and does not draw it).
