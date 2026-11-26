# Minimal viable build: Rescue audit

Step 1 of the minimal viable build (docs/ROADMAP.md): Rescue from start to finish. Each screen and scene on the path, what plays, whether it works, and the owner's decision (keep, drop, or change).

Recorded on the Linux test machine, 2026-11-23 and 2026-11-26. The harness script is `tools/qa/harness/session-2026-11/audit_rescue_endings.sh`. Status: ✅ works, ❌ broken, ⚠️ works but needs a decision or a check on the owner's PC.

| # | Screen or scene | What plays | Status | Owner's decision |
| --- | --- | --- | --- | --- |
| 1 | Boot: the game's intro | The game's own intro scenes: a white backdrop with flying blocks, the sphere, the robot | ✅ | |
| 2 | Title | The original logo, with BlockPlanet's copyright line under it | ✅ | |
| 3 | Attract demo (title left alone) | The game's demo play, **with the original HUD** (DEMO lettering, old hearts, the magic bowl) | ⚠️ the only place the old HUD still shows | keep / BlockPlanet's HUD / drop the demo? |
| 4 | Who is playing | BlockPlanet's profile screen | ✅ | |
| 5 | Rescue menu | Campaign, Score Attack, Time Attack, Casual, each with a **"(Viable)"** developer tag | ⚠️ the tags | remove the tags? |
| 6 | Stage start | The loading cover, then play with BlockPlanet's HUD | ✅ | |
| 7 | Stage clear and the next stage (1:1 to 1:2) | The core opens, the friend, score pops, the friend escapes, a short fade, the next stage | ✅ | keep as the original? (about 10 s; a skip button was an option, see docs/research/blockplanet-stage-transition.md) |
| 8 | Losing a heart | The heart empties in BlockPlanet's HUD; the held power-up is kept | ✅ (the old heart animation peeking out from behind the HUD, seen by the owner, not seen here: to check with a screenshot) | |
| 9 | Game over | The hearts go, the game-over shell of blocks round the sphere, the red grid, BlockPlanet's Game Over panel: Try Again, Training, Exit to Rescue Menu | ✅ (the shell looked wrong on the owner's PC before: black blocks; roadmap) | |
| 10 | End of Episodes 1–4, 6, 7, 9 | Core, friend, "NICE JOB", then BlockPlanet's "Episode N Cleared" panel: Carry on to Episode N+1, Exit to Rescue Menu | ✅ | |
| 11 | End of Episodes 5 and 8 | The game's ending: a curtain call (any key skips it), then BlockPlanet's "Episode N Cleared" panel | ✅ fixed 2026-11-23 (was BlockPlanet's empty grid for minutes); ⚠️ Exit to check on the owner's PC | keep the curtain call after 5 and 8? |
| 12 | End of Episode 10 | The curtain call, then BlockPlanet's credits (to Sappy), then the Rescue menu | ✅ | |

## Still to look at in step 1

- Rescue's other menu screens: the episode list, the stage list, Continue.
- Casual: the stages the owner could not open (details to come).
- The pause menu in Rescue.
