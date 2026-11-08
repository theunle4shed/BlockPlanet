# Style review: brief for the next session (2026-11-08)

The owner wants one consistent style across BlockPlanet: menus, HUD, cards and every other screen. This brief starts that work in a fresh session.

## The job

1. **Capture** every BlockPlanet screen and card at 16:9 and 4:3 (and one small window), with the QA harness (`tools/qa/harness/nav.sh`, `BLOCKPLANET_UI_TEST=1`, `BLOCKPLANET_UNLOCK_ALL=1`). List every piece of text with its size, weight, colour, spacing and where its style is set.
2. **Propose** `docs/STYLE.md`: text roles (sizes, weights, colours), panels (fill, border, radius, padding), spacing, numbers and hints, for menus, HUD and cards together. For each place that does not fit, say what it becomes. Show it to the owner as a page with before and after examples, and let them decide.
3. **Apply** it across `assets/ui/menu.rcss` and `assets/ui/hud.rcss` (RmlUi has no CSS variables: shared roles become shared classes or grouped selectors), so the Text size setting scales everything evenly. Check every screen again at the same sizes.

## Already decided

- **Menu type scale** (top of `assets/ui/menu.rcss`): Hero 96 bold (PAUSED), Display 64 bold (titles), Item 40 (34 in long lists), Subtitle 26 yellow, Body 26, Note 22. A starting point; the review may adjust it.
- **Colours:** menu items orange `#F5821F`, the focused item white `#F8F8F8`; notes, values and hints yellow `#E8B818`. In card and speech text: body orange, headings yellow and bold, **game terms light blue `#86D8FF`** (glowing where a card first introduces one). Speech is italic.
- **No all-capitals text**, except a warning (AGENTS.md); Title Case for labels, sentence case for sentences.
- **Modern, TV-like, controller-first** menus; no imitation of the 1990s menus (AGENTS.md).

## Screens and parts to cover

Menus: main, Who is playing?, profiles, profile, name, character, confirm, Options, Graphics, Audio, Solo, Story, first-time Story, Training list, chapters, stages, Local and its screens, Online, How to Play, pause. In play: the Story HUD (left and right panels, including score, gains and combo), the Versus HUD (Player 1 and Player 2 panels), Training's HUD row and speech panel, the rules and goal cards (Hide & Seek, Lines, Versus), the title screen's notice. The game's own screens still shown: the game over menu (Try Again, Exit, Training) and Versus' end menu (Continue, Exit).

## Related observations to fix along the way (the owner, 2026-11-08)

- **The game's own menus, redrawn in BlockPlanet's font** (game over, Versus' Continue and Exit, earlier the original main menu), jump left and right every few frames on Windows. Probably the original's sliding or staggered item positions captured on different frames; BlockPlanet should place them steadily (or replace these menus with its own panels).
- **Game over and Versus' end:** hide the HUD panels there, with a quick fade.
- The chapter and stage lists were too large (fixed: compact rows, scrolling). Check every list the same way.
