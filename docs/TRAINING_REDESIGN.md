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
3. **Pages combined.** Pages that only show more text, with no `-3`, `-4` or `-5` before the next break, are joined into one panel. If the text is too tall, it scrolls with the stick or d-pad. A then moves past all of them at once (BlockPlanet presses A for each joined page). Pages with a demonstration or a scripted action stay separate, as the game needs them in turn. The first page ("Press down or the A button to page through the training text") is dropped: the panel's "A More" hint says the same.
4. **New steps where the game says nothing.** BlockPlanet adds its own text pages for what the HUD shows but the lessons never explain: the drop timer, the next pieces, the progress bar, Time and Score Attack's clock. These pages pause the lesson (held on the current page) and highlight the element. They cannot demonstrate moves: the game's demonstrations are its own code.
5. **Shorter lessons in the list.** Training's list could show each level of a lesson as its own step (Moving and sliding, Lives, Combos, Magic; Crystal pieces, X count, Fuse combos), each marked done. This needs research into starting Training at a later level.
6. **Wheels.** He is already the game's 3D model (drawn as a scene each frame, in his training hat), not a picture. He could be larger, placed bottom right as the speaker, and turn or nod as each page appears, using his own scene's animation.

## Suggested order

| Step | Size |
|---|---|
| 1. Hide the original HUD in Training and show Story's HUD at the bottom, revealing elements as above | Medium |
| 2. Combine text-only pages, with scrolling | Medium |
| 3. BlockPlanet's own pages for the HUD elements the lessons skip | Medium |
| 4. The wide-screen layout, with the true camera aspect | Research, then medium |
| 5. Lesson steps in the list, and Wheels animating | Research |
