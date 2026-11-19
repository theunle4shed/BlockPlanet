# From one stage to the next

Measured 2026-11-19 (the owner's question: could the next stage be prepared in advance?), on the test machine (Linux, software Vulkan) with `BLOCKPLANET_TRANSITION_LOG=1`, `BLOCKPLANET_MUSIC_LOG=1` and `BLOCKPLANET_SPHERE_RESEARCH=1`, in Rescue 1:1 to 1:2 and 10:5 to 10:6. Each stage was won with `poke cmd:research_win` (which now paints every layer one colour, so stages of many layers clear too), with screenshots every 2 seconds afterwards.

## What happens

| After the win | On screen |
| --- | --- |
| 0 to about 7 s | The core bursts open with a score pop; the rescued friend inside the core |
| about 7 to 9 s | "NICE JOB" |
| about 9 to 10.5 s | Black: the game's own fade (BlockPlanet's loading cover is not shown); the next stage is set up here |
| about 10.5 to 11 s | The next stage on screen and playable (a piece in hand 0.3 s after the stage number changes) |

The same in both: about 10 to 11 seconds from the win to the next stage being playable, whatever the number of layers.

## The next stage's set-up is already fast

| Transition | Next stage's build | Song |
| --- | --- | --- |
| 1:1 to 1:2 | 11.7 ms | track 1 carries on |
| 10:5 to 10:6 | 15.3 ms | track 6 carries on |

A stage started from the menus takes about 250 ms, nearly all of it the song loader's 10-retrace wait when the song changes ([sphere generation](blockplanet-sphere-generation.md)). Between stages of an episode the song is the same, the loader returns early, and the whole set-up is 12 to 15 ms. So preparing the next stage in advance would save almost nothing: the time is the game's stage-end sequence.

## Also seen

- **Fixed (2026-11-19, the owner's request):** the episode's clock now stops when the core opens and runs again with the next stage. Before, **the episode's time kept running through the stage-end sequence** (the HUD's Time went from 0:11 to 0:23 across 1:1's ending). For Time Attack every run includes the same sequence, so times stay comparable, but about 10 seconds a stage (about 100 s an episode) is the celebration, not play.

## If the transition is to be shorter

The places with time to save are the game's own: the core and friend sequence (about 7 s), "NICE JOB" (about 2 s) and the fade (about 1.5 s). Options, for the owner to choose: a button to skip the sequence, a shorter version, or leaving it as the original. Time Attack could also stop its clock during the sequence.
