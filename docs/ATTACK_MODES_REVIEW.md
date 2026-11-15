# Rescue's Time Attack and Score Attack, and combo pop-ups: how they work today (2026-11-15)

For the owner to review before anything changes. Lines' own Time Attack and Score Attack wait for this (docs/MODE_MATRIX.md). The detail behind each point is in docs/HUD_DESIGN.md.

## Getting there

- Solo > Rescue (once Episode 1 is done) > **Score Attack** (open after Episode 1) or **Time Attack** (open after Episode 3).
- Each lists Rescue's ten episodes. An episode offers **Whole Episode**, and its single stages once the whole episode has been played through.
- The first time a profile plays each one, a start card explains it; after that the explanation is on the pause screen.

## Time Attack

- Rescue's stages, played as normal: hearts, game over and all.
- **The clock counts up.** Only play time counts: not the pause, not the time between stages.
- **Right-hand panel (LiveSplit style):** the run's total time, the net difference against your bests so far (green ahead, red behind), then a row per stage with its time and difference.
- **Bests:** each stage's best time, and the best whole-episode time (only for a run from stage 1). Each stage is compared with its best as it was when the run started.
- **A single stage** shows its time, your best and the difference, then the results screen (Next Stage, Try Again, Back).
- **A whole episode** ends on the "Episode Cleared" panel with the episode's time against your best.
- Trophies: Speedrunner (a whole episode), Photo Finish (the final episode), Out-Ran the Maker (beat the developer's time on every stage; the developer's times are not recorded yet).

## Score Attack

- Rescue's stages and clear condition, with a **countdown clock** under Health ("Time left").
- **Each stage's own time:** 2:00 for a one-layer sphere, plus 10 seconds a layer (2:00 to 2:50 in Rescue).
- **Combos add time:** a second for each block past two, up to ten seconds; less the longer you've been on the stage (all of it in the first minute, two thirds in the second, a quarter in the third, none after).
- **Clearing a stage:** its whole minutes left carry to the next stage, and the leftover seconds become points (1,000 a second). The last stage, or a single stage, turns all its time into points.
- **At 0:00** the run ends with the game's own game over.
- **Right-hand panel:** the run's points, the net difference against your best points per stage (more is better), then a row per stage.
- **Bests:** each stage's best points.
- Trophies: Overtime (play two minutes past a stage's own time), Point Hunter (a whole episode), Last Stand (the final episode), Out-Scored the Maker.

What the screenshots show (sent separately; not kept in the repository): in Time Attack after two stages, the total 0:45 and the stage rows 1:1 0:16, 1:2 0:16, 1:3 0:11. In Score Attack, "Time left 6:09" (minutes carried over) and the points per stage.

## Combo pop-ups and other floating text today

| What | Where | Today |
|---|---|---|
| The game's floating points over the sphere ("400", "500" after a drop or a small clear) | Over the sphere | Shown in every mode (the game's own) |
| The game's score bubbles during a combo (1K blue, 2K purple, 3K pink, 5K red…) | Over the bricks as they break | Shown (the game's own); decided 2026-11-08 to hide them in Rescue, which hides the score, and redraw them sharp where they stay; not done yet |
| The large grey 3D number after a combo ("30100" after a combo of 21) | Over the sphere | Shown (the game's own) |
| "Nice Job" at a stage's end | Over the sphere | Shown (the game's own) |
| Combo counter: "Combo" counting up, then "Last combo", gold with "+ Magic" at 20 or more, and the stage's best | The HUD, right | Shown in Rescue and Casual |
| The last points added ("+20,000", up to five) | Under the score in the HUD | Shown in Rescue and Casual |
| BlockPlanet's own pop-ups over the sphere | Over the sphere | Built but off (decided 2026-11-06); `BLOCKPLANET_POPUPS=1` shows points for testing |
| Time added by a combo in Score Attack | — | Not shown anywhere (the clock just jumps) |

## Questions for the owner

1. Time Attack: keep hearts and game over, or should a stage just carry on (as Casual) since only time matters?
2. Score Attack: are the clock rules right (2:00 plus 10 s a layer; combos add up to 10 s, less as the stage goes on; whole minutes carry over)?
3. Score Attack: should a combo show the time it adds ("+4 s") near the clock?
4. Pop-ups: in Rescue, which of the floating texts above should stay, go, or be replaced by BlockPlanet's own (and in what style)?
5. The developer records ("beat the maker" trophies): will you record your own times and scores, or should those trophies go?
