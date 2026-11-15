# Game modes: how each one looks and works (2026-11-14)

The owner's request: every mode should start, explain itself, pause, end and exit the same way, so the player learns one pattern. The rule (the owner, 2026-11-14): everything around the play (start-up, instructions, pop-ups, win and game-over screens, menus) can change freely to be consistent; how each mode plays stays as the original, apart from bug fixes and small improvements.

This is the state as the code stands, read mode by mode (not every cell is confirmed in play; "?" means the code does not say). Rescue is the reference mode (docs/ROADMAP.md, "One game, many rulesets").

## The matrix

| | Start | Instructions | HUD | Pause: restart item | Stage clear / win | Game over / loss | Continue checkpoint | Records | Exit goes to |
|---|---|---|---|---|---|---|---|---|---|
| **Rescue** | Solo > Rescue > Campaign > chapter > Whole Episode or a stage | How to Play (pause) | Full: health, magic, next; mode, stage, progress, time, combo, score | Restart Chapter (asks) | Game's own sequence, next stage; episode end: our results panel (Next Episode, Exit) | Game's GAME OVER, then our panel: Try Again, Training, Exit | Yes, at each episode's start | Stage and episode times | Chapter N, or Main Menu after Continue |
| **Rescue: one stage** | Stage list | As its variant | As its variant; Time/Score Attack show "Your time/score" | Restart Chapter (restarts stage 1, not the chosen stage) | Back to the stage list after 2.5 s | Our panel | No | Per variant | Chapter N |
| **Casual** | Rescue > Casual > chapter | How to Play | As Rescue | Restart Chapter | As Rescue | Never (hearts come back) | Yes | None (but its list shows Campaign bests) | Chapter N |
| **Time Attack** | Rescue > Time Attack > chapter | How to Play | Left as Rescue; time, delta, splits; no combo or score | Restart Chapter | Carries on into the next episode | Our panel | No | Stage and episode times | Chapter N |
| **Score Attack** | Rescue > Score Attack > chapter | How to Play | Clock under health; score, delta, splits | Restart Chapter | Episode ends: back to the menus after 3 s | Clock runs out: game over, our panel | No | Points per stage | Chapter N |
| **Training** | X on Rescue, Campaign or Casual; X on Puzzle | Wheels' speech in our card panel | One row along the bottom, parts appear as taught | Restart Lesson (not in Advanced), Skip Lesson | Game ends the lesson, back to the menus | ? | No | Completion only | "Rescue" (wrong: the screen said Training) |
| **Puzzle** | Solo > Puzzle > puzzle list | How to Play | Drops and slides left; puzzle, time, best | Restart Level | Straight on to the next puzzle | ? | No | Time and moves | Puzzles |
| **Hide & Seek** | Solo > Hide & Seek (no list) | Goal card in our panel; How to Play | As Rescue on the left; level, goal, time, best | Restart Chapter (asks) | Game's own, next level | Our panel | No | Time per level (progress kept but unused) | Solo |
| **Versus (computer)** | Local > Rescue > Versus > Computer | Rules card in our panel; How to Play ("VS") | Two columns at the edges; no mode label | Restart Level | "Match Over" panel: Continue, Exit | "Game Over" panel | No | Rounds won and lost | Versus |
| **Lines** | Solo > Lines (no list) | Rules card once per profile, then the rules on the pause screen; How to Play with controls | Health, magic, no Next; stage, progress, time, best | Restart from 1:1 | Game's own, next stage; episode end: our results panel (Next Episode, Exit) | Our panel | No | Times per stage | Solo |
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
| Hide & Seek | **Yes, before every level** (BlockPlanet now plays it on a run's first level only), one per goal type: MULTI (a picture sphere flies in, then the word sweeps across), DRILL (a drill and falling letters), BRICK (letters on bricks), TOWER (a block tower rises), HIDE & SEEK (eyes peep from a hole), UNIQUE (a ribbon), SHIFT (scattered letters). CONNECT and CRYSTAL TOWER not yet seen. Our HUD shows over the animation. | Our goal card, A Start |
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
- **Rescue, 2026-11-15:** an episode's end in a whole-episode run (Rescue and Casual) shows BlockPlanet's results panel on a green grid in place of the game's CONTINUE / EXIT menu: "Episode Cleared", the variant and episode, the episode's time against the best (a run from stage 1), then Next Episode and Exit to Rescue. The game's rescue animation plays first, and after Next Episode the game's own "Episode 2" card shows without the HUD over it (before, the pause grid covered both the menu and the card). 
- **Rescue, 2026-11-15 (2):** the start card (the owner's go-ahead): a run launched from the menus (an episode, a single stage, Continue; not a restart) waits on a card over the pause grid: the variant ("Rescue", "Casual", "Time Attack", "Score Attack"), where it starts ("Episode 2", "Stage 3:4", "Episode 2 · from stage 2:5"), the goal ("Dig down to the core: uncover a 4 × 4 square of it to rescue the friend inside", with the square size the stage sets and a line for the variant), then Start; Back (or Escape) goes back to the menus. The game is paused under it, so no time passes. `BLOCKPLANET_START_CARD=0` turns it off. The owner's refinement (2026-11-15): the card shows only the first time a profile plays each game type (Rescue, Casual, Time Attack, Score Attack, remembered in the profile's save); after that the same goal is on the pause screen, under "Rescue · 1:1". Rescue now has every part of the shared pattern except the per-mode intro animation.
- **Hide & Seek, 2026-11-15:** in the shared pattern (the owner's go-ahead). Its intro animation plays once per start: on the run's first level; later levels (and a restart, the same run) skip it, as pressing A does. Each goal's card (Any Picture, Five Drills, Help Them Escape, Four Parts, Unique Shape, Brick Drop, Connected Pictures, Tower, Crystal Tower) shows the first time a profile meets that goal; after that it closes by itself, and the pause screen shows the goal in full under "Hide & Seek · Level n". Restart is "Restart from Level 1". No progress bar, by design (it would hint where the pictures are). `BLOCKPLANET_HS_SKIPS=0` leaves the intros and cards as the game shows them. 
- **Hide & Seek, 2026-11-15 (2):** an episode is 30 levels; after level 30 the game plays the rescue scene, then the next episode's card ("Congratulations, you finished the first episode…", the pause row at 100), with no menu of its own. BlockPlanet's results panel now comes first, as Rescue's: "Episode Cleared", "Hide & Seek · Episode n", the episode's time against the best (a run from level 1), then Next Episode (the game's card shows; A carries on) and Exit (the card is closed and play left through the next level's pause, under the grid). Level bests are now kept per episode (those from before are episode 1's), and level 30's best is saved (it was lost as the level number went back to 1). Research: `poke cmd:research_hs_level:N` starts Hide & Seek at level N. The game shows no goal card for episode 2's first level (Any Picture), even with BlockPlanet's skipping off.
- **Puzzle, 2026-11-15:** checked puzzles 1, 15, 40, 70 and 100: every puzzle has the same goal (clear every brick inside the frame); what changes is the board and the drops and slides given (puzzle 40: 1 drop, 8 slides; puzzle 100: 1 drop, 0 slides). The owner's choice: one card, each puzzle's limits. The start card shows the first time a profile plays Puzzle ("Puzzle 40", the goal, "This puzzle: 1 drop, 8 slides"); after that the same text is on the pause screen. Restart is "Restart Puzzle". 
- **Puzzle, 2026-11-15 (2):** the results panel. A solved puzzle goes straight on to the next one in the game (about 2 s, no animation or menu), so BlockPlanet pauses the next puzzle as it starts and shows "Puzzle Solved": the puzzle, its time against the best and the moves used, then Next Puzzle (resume), Try Again (the solved puzzle again) and Exit. Fixed on the way: the game's clock runs on from one puzzle to the next, so each puzzle's time (HUD and bests) now counts from its own start. Painting the bricks one colour clears the frame but does not solve a puzzle (nor does the won flag `+0x3236`); puzzle 1 was solved for real in the test setup (see the research notes).
- **Lines, 2026-11-15:** in the shared pattern (the owner: one-player modes first; Versus paused). Lines has episodes of 10 stages, as Rescue: after 1:10 the game shows the same CONTINUE / EXIT menu on the pause row, then the next episode's card ("Congratulations you passed the first episode…"), then 2:1. That menu was hidden under the pause grid; BlockPlanet's "Episode Cleared" panel ("Lines · Episode n", Next Episode, Exit) now replaces it. The rules card (two pages; it shows only at the start, not before each stage) shows the first time a profile plays Lines, then closes by itself; its second page no longer shows the game's text from mid-sentence. The pause screen shows the rules; Restart is "Restart from 1:1"; How to Play gives the controls. Fixed: stage 10's best was never saved, and a stage's time carried on when Lines was started again. No episode time yet.
- **Lines, 2026-11-15 (2):** Lines gets Rescue's menu shape (the owner): Solo > Bonus > Lines plays Episode 1 straight away until it is done, then opens Lines' menu: Campaign (on from the next stage not yet completed), Time Attack and Score Attack (both still play as Campaign: their own clocks are the next steps), each with Lines' five episodes and their stages. Lines can now start at any episode and stage (the direct start no longer forces 1:1). A stage played on its own ends on "Stage Cleared" ("Lines · 2:3"), as Rescue's. Lines' stages and episodes completed are now recorded. Restart is "Restart Episode" or "Restart Stage". Single stages (Rescue's and Lines') open only once the whole episode has been played through (the owner's rule), except in the main build, where everything is open for testing.
