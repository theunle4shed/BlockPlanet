# Game modes: how each one looks and works (2026-11-14)

The owner's request: every mode should start, explain itself, pause, end and exit the same way, so the player learns one pattern. The rule (the owner, 2026-11-14): everything around the play (start-up, instructions, pop-ups, win and game-over screens, menus) can change freely to be consistent; how each mode plays stays as the original, apart from bug fixes and small improvements.

This is the state as the code stands, read mode by mode (not every cell is confirmed in play; "?" means the code does not say). Rescue is the reference mode (docs/ROADMAP.md, "One game, many rulesets").

## The matrix

| | Start | Instructions | HUD | Pause: restart item | Stage clear / win | Game over / loss | Continue checkpoint | Records | Exit goes to |
|---|---|---|---|---|---|---|---|---|---|
| **Rescue** | Solo > Rescue > Campaign > chapter > Whole Episode or a stage | How to Play (pause) | Full: health, magic, next; mode, stage, progress, time, combo, score | Restart Chapter (asks) | Game's own sequence, next stage; episode end: game's own Continue/Exit menu | Game's GAME OVER, then our panel: Try Again, Training, Exit | Yes, at each episode's start | Stage and episode times | Chapter N, or Main Menu after Continue |
| **Rescue: one stage** | Stage list | As its variant | As its variant; Time/Score Attack show "Your time/score" | Restart Chapter (restarts stage 1, not the chosen stage) | Back to the stage list after 2.5 s | Our panel | No | Per variant | Chapter N |
| **Casual** | Rescue > Casual > chapter | How to Play | As Rescue | Restart Chapter | As Rescue | Never (hearts come back) | Yes | None (but its list shows Campaign bests) | Chapter N |
| **Time Attack** | Rescue > Time Attack > chapter | How to Play | Left as Rescue; time, delta, splits; no combo or score | Restart Chapter | Carries on into the next episode | Our panel | No | Stage and episode times | Chapter N |
| **Score Attack** | Rescue > Score Attack > chapter | How to Play | Clock under health; score, delta, splits | Restart Chapter | Episode ends: back to the menus after 3 s | Clock runs out: game over, our panel | No | Points per stage | Chapter N |
| **Training** | X on Rescue, Campaign or Casual; X on Puzzle | Wheels' speech in our card panel | One row along the bottom, parts appear as taught | Restart Lesson (not in Advanced), Skip Lesson | Game ends the lesson, back to the menus | ? | No | Completion only | "Rescue" (wrong: the screen said Training) |
| **Puzzle** | Solo > Puzzle > puzzle list | How to Play | Drops and slides left; puzzle, time, best | Restart Level | Straight on to the next puzzle | ? | No | Time and moves | Puzzles |
| **Hide & Seek** | Solo > Hide & Seek (no list) | Goal card in our panel; How to Play | As Rescue on the left; level, goal, time, best | Restart Chapter (asks) | Game's own, next level | Our panel | No | Time per level (progress kept but unused) | Solo |
| **Versus (computer)** | Local > Rescue > Versus > Computer | Rules card in our panel; How to Play ("VS") | Two columns at the edges; no mode label | Restart Level | "Match Over" panel: Continue, Exit | "Game Over" panel | No | Rounds won and lost | Versus |
| **Lines** | Solo > Lines (no list) | Rules card in our panel; How to Play (short, no controls) | Health, magic, no Next; stage, progress, time, best | Restart Level | Game's own, next stage | Our panel | No | Times (last stage of a chapter never saved) | Solo |
| **Time Trial** | No menu item | None | The original HUD | Restart Level | ? | ? | No | None | ? |
| **Minefield** | Solo > Minefield > arena | None (removed from pause) | Right panel only: arena, mines, time, best | New Field (no question) | Result, then the game's stage end, back to the menus | Same as a win | No | Best time per arena | ? |

## Intro animations today (captured 2026-11-14)

Each mode started from the menus and its first 18 to 24 seconds captured.

| Mode | Animated intro | Then |
|---|---|---|
| Rescue | None: the loading cover ("Rescue"), then play | The game's own "Episode n" text at an episode's start |
| Casual, Time Attack, Score Attack | None (as Rescue) | |
| Training | None: Wheels starts talking at once | Wheels' speech in our card |
| Puzzle | None; each puzzle has its own background | Play at once |
| Hide & Seek | **Yes, before every level**, one per goal type: MULTI (a picture sphere flies in, then the word sweeps across), DRILL (a drill and falling letters), BRICK (letters on bricks), TOWER (a block tower rises), HIDE & SEEK (eyes peep from a hole), UNIQUE (a ribbon), SHIFT (scattered letters). CONNECT and CRYSTAL TOWER not yet seen. Our HUD shows over the animation. | Our goal card, A Start |
| Lines | None | Our rules card, A Start |
| Versus | None: play at once | |
| Minefield | None | Play at once |

So only Hide & Seek has animations, and its play before every level, not once per mode start. Every other mode needs one made (the owner's plan: one per mode, once per start, skippable once seen).

## The clearest inconsistencies

1. **Instructions:** How to Play is missing in Training, Time Trial and Minefield; Lines' is two sentences without the controls the others give. Goal and rules cards use our panel in Hide & Seek, Versus and Lines only; Rescue's "Episode n" cards and Puzzle do not.
2. **Names:** the campaign is "Campaign" in its menus but "Rescue" on the HUD; every variant's pause line says "Rescue" (not Casual, Time Attack or Score Attack); Versus is "Versus", "VS" and "VS Rules" in different places, and Time Trial's pause line says "Score Attack".
3. **The restart item:** Restart Chapter (with a question), Restart Level, Restart Lesson (missing in Advanced) or New Field (no question); in a single Rescue stage, "Restart Chapter" goes to stage 1, not the stage chosen.
4. **How a mode ends:** single stages and Score Attack go back to the menus by themselves; Rescue and Time Attack episodes carry on into the next; Puzzle moves on to the next puzzle; Minefield's loss plays the game's stage-clear sequence. Only Time and Score Attack single stages show a result.
5. **End screens:** our panel covers game over and Versus' end only; Rescue's episode-end Continue/Exit is still the game's own menu; "Match Over" on a Versus win but "Game Over" on a loss.
6. **The HUD:** combo and score only in Rescue and Casual; progress bar missing in Hide & Seek, Puzzle, Minefield and Versus; no Next in Lines; no mode label in Training and Versus.
7. **Exit labels** follow the screen's file title, not what was on screen (Training says "Exit to Rescue").
8. **Records and trophies:** Lines records no progress and has no trophies, and the last stage of each chapter's best is never saved; Hide & Seek's progress is kept but no menu shows it; Casual's stage list shows Campaign's bests; two trophy conditions have nothing that sets them (combos, magic used; One and Done's drops).
9. **Continue** exists only for Rescue and Casual episodes.
10. **Unreachable:** Time Trial has no menu item; two-player Versus, Lines for two and Cooperative are "coming later".

## A shared pattern to bring them to (proposal, for the owner)

Every mode, the same five moments, with Rescue's look:

1. **Start:** an intro animation of the mode's own (the owner, 2026-11-14): it plays once when a mode is started, not before every level (Hide & Seek now plays its title before each level); in Adventure it plays each time the next stage is launched; it can be skipped once the player has seen it (remembered per profile). Then a card in our panel with the mode's name, the stage and its goal in one line ("Hide & Seek · Level 3: Find five drills"), and A Start. The same card for every mode, Rescue's episodes included.
2. **Instructions:** How to Play in every mode's pause, the same layout (goal, then controls, then the mode's special rules).
3. **During play:** one HUD layout: on the right the mode, the stage, the goal or progress, time and the mode's score; on the left what the player holds (health, magic, next, or drops and slides). A part is left out only when the mode has nothing for it.
4. **Pause:** Resume, Restart (one name, "Restart Stage", or "Restart Episode" where a run is several stages, always asking first), How to Play, Options, Exit to <the screen the player came from, by its shown name>.
5. **The end:** one results panel for every end, win or lose: the result ("Cleared", "Game Over", "Match won"), the time and score against the best, and the same choices everywhere (Next or Try Again, Exit). The game's own animation (NICE JOB, GAME OVER) plays first, as now.

## Questions for the owner

1. Is the shared pattern above the right shape?
2. The end of an episode, a puzzle or a stage: back to the menus each time with a results panel, or carry straight on (as Rescue and Puzzle do now) with the panel offering Next?
3. Should Time Trial (the original's timed mode) come back as a menu item, or stay replaced by Score Attack?

## Progress

- **Rescue, 2026-11-14:** episodes are called episodes everywhere (the game's own word; the menus said chapters); the pause and end lines name the variant ("Casual · 1:3", "Time Attack · 2:5"); the restart item is "Restart Episode" for a run and "Restart Stage" for a single stage, both asking first, and Restart Stage now restarts the stage chosen (it went back to stage 1). Still to do for Rescue: the start card, and the results panel at a stage's and an episode's end.
- **Rescue, 2026-11-14 (2):** a stage played on its own ends on a results screen over the stage list: "Stage Cleared", the variant and stage, the time (or points) against the best ("New best" when beaten), then Next Stage, Try Again and Back to Episode N. Whole episodes keep flowing from stage to stage (the owner's choice); the results panel at an episode's end and the start card are still to do.
