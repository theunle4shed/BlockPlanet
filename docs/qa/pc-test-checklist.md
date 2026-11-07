# PC test checklist (Windows)

For the owner's playtests. Tick what works, note what doesn't (what you did, what you saw, the screen shape). Screenshots help. Built from the changes since the last Windows test (see `CHANGES-SINCE-BLOCKSPHERE.md`).

## Before you start

- [ ] Build this branch and start it; note the build and the screen size.
- [ ] If something crashes, see **Capturing a crash** at the end before restarting.

## Start-up and title

- [ ] Logo screens play; no copyright text over them.
- [ ] Title: 3D letters, orange ring, light from the white square to the left, Turbine, golden Wheels in the square, three Bombs aimed at the breach.
- [ ] Copyright notice and disclaimer readable at the bottom; check a window narrower than 16:9 and a wide one.
- [ ] Press Start: BlockPlanet's main menu opens.

## Menus

- [ ] Main menu: Continue (only after an interrupted game), Solo, Local, Online (coming later), Profiles, Options, Exit.
- [ ] Each item shows a description underneath while selected.
- [ ] Solo: Story, Hide & Seek, Puzzle, Lines, Multiphasic (coming later).
- [ ] Story: Campaign, Time Attack, Score Attack, Casual; each opens the episode list, then Whole episode or a stage.
- [ ] Local: Story (Versus, then player 2: Computer), Lines and Custom marked coming later.
- [ ] Back works everywhere; controller and keyboard both work.

## Story (Campaign)

- [ ] Whole episode 1: HUD on both sides (health, drop timer, magic box, next pieces; stage, progress bar, episode time).
- [ ] Magic: the item turns in the box; using it flies from the box.
- [ ] Win a stage: the next stage follows (1:1 to 1:2).
- [ ] Single stage (e.g. 1:5): starts at 1:5; after winning, back to the stage list with the time shown.
- [ ] Pause: BlockPlanet's pause panel; Restart chapter asks first; Quit to menu returns to BlockPlanet's main menu.
- [ ] New profile: Solo > Story first asks "Start with Training" or "Go straight to Story"; the next time, Story opens directly.
- [ ] Training: in Solo, with Story selected, the description ends with your X button (or T on the keyboard) and "Training". Press it: Training lists Basics and Advanced. Each starts and returns to the menus when done. Puzzle's X starts Puzzle Training.
- [ ] Graphics > Text size: Small, Default, Large and Larger change Training's speech bubble; the bubble stays on screen at Larger.
- [ ] Training's bubble scrolls instead of leaving the screen: try Text size Larger on a short or narrow window. The right stick, Page Up / Page Down or the mouse wheel scroll it, and "Scroll" shows in its hint only then.
- [ ] Crash report (Windows): start BlockPlanet with `BLOCKPLANET_CRASH_TEST=20` set; it closes after 20 seconds. Start it again: a message says where the crash report is. Open it: it should list the build, what the game was doing and a stack with module names.
- [ ] Resize the window while playing (Story, Training, a card showing): the HUD, the speech panel and the menus should follow the new size at once, with nothing left in the old place.
- [ ] Training's HUD: a row along the bottom, empty at first; Next appears when Wheels shows the shadow, Health at Lives, To the core at Game objective, Magic at Magic, each glowing briefly. Advanced adds the X count; Puzzle Training shows drops and slides at its last page.
- [ ] Training's pages: one A press moves past text already shown; the first "press down or A" page is skipped; demonstrations ("Watch") still play.
- [ ] Hide & Seek, Lines and VS cards: BlockPlanet's panel (no tail) with the whole text at once, Hide & Seek's new titles (Any Picture, Five Drills…), "sphere" not "ball", and BlockPlanet's HUD behind it (no original HUD).
- [ ] Training cards: the button symbols match your controller (bumpers, triggers and Magic too), apostrophes look right, and the text stays inside the box.
- [ ] Game over: GAME OVER, then Try Again / Exit / Training in BlockPlanet's font.
- [ ] Quit mid-episode, restart the game: Continue offers that episode; it starts at its first stage with the score and hearts it began with.

## Time Attack and Score Attack

- [ ] Time Attack: splits per stage; play a stage twice and see the ± against your best (green ahead, red behind).
- [ ] Score Attack: Time left under health (2:00 for thin spheres, more for thicker); combos add time; clearing a stage carries whole minutes and turns the rest into points; at 0:00 the game ends.
- [ ] Casual: losing a heart gives it back; no records.

## Other modes

- [ ] Hide & Seek: goal card readable at the start; HUD shows the goal in words and a timer; no progress bar.
- [ ] Puzzle: the Puzzles list; drops and slides left; time; a solved puzzle shows its best. Note the line across the bottom of the screen (known).
- [ ] Lines: rules card readable; HUD without Next.
- [ ] VS CPU (Local, Story, Versus, Computer): a column per player at the screen edges.

## Screens and options

- [ ] Try 16:9, a 4:3 window, an ultrawide window and a tall window: HUD, menus and title stay readable.
- [ ] Graphics > HUD panels: Near, Screen edges, Follow.
- [ ] Audio options and music still work.

## Also watch for

- Anything odd in the endings, credits or VS (the game's 3D scenes can now use 8 MB of memory).
- Slowdowns, stutters, wrong colours, text in the wrong place.

## Capturing a crash

BlockPlanet writes a log for every session in its data folder, under `diagnostics` (one `.jsonl` file per run; the newest is the last session). Send that file along with what you were doing.

For a full crash dump, Windows can save one itself (no BlockPlanet code needed), once:

1. Open the Registry Editor and go to `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps`.
2. Create a key named after the executable (e.g. `tetrisphere-m1.exe`).
3. In it, add `DumpFolder` (expandable string, e.g. `%LOCALAPPDATA%\CrashDumps`) and `DumpType` (DWORD, `2` for a full dump).

After a crash, the `.dmp` file in that folder shows exactly where it failed.
