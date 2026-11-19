# The drop timer: when the game drops the piece itself

Found 2026-11-18 and 2026-11-19 by reading the generated code and logging play (`BLOCKPLANET_DROP_LOG=1`). Addresses are player 1's context (`0x80110220`) unless shown in full. BlockPlanet's HUD follows these rules in `tetrisphere::DropClock` and `drop_run` (`src/platform/story_hud.cpp`).

## In short

Holding a piece starts two things in turn. First a **timer** runs. At its **first warning** a **fall** starts: the view closes in on the sphere. At the **second warning** the fall speeds up. The piece is dropped, and a heart lost, when the fall ends. The timer's own end is rarely reached.

**Where the piece would land makes no difference.** In Rescue 10:10 a 5 × 5 patch under the cursor was emptied down to layer 1 before each piece (`BLOCKPLANET_DROP_TEST_KEEP=1`). The pieces landed at height 2 instead of 7, and the time from the second warning to the drop was 0.80 and 0.90 s. On the full sphere it was 0.83 and 0.85 s.

**Both parts run in real time.** The game runs 60 logic steps a second whatever its drawing rate: below 60 frames a second it runs several steps per drawn frame (`0x80160C64`, [blockplanet-frame-pacing.md](blockplanet-frame-pacing.md)). The timer and the fall both move in those steps. So the time to a drop is the same on the N64 (about 20 to 34 frames a second) and in BlockPlanet at 60. In the test machine's runs the game drew every 2 to 4 retraces, yet the fall kept 60 steps a second.

## The rules, each 60 Hz step

| Part | Where | Rule |
| --- | --- | --- |
| Timer | `+0x2408` (float) | Gains the timer speed × the steps this frame (`func_800CD3B8`, `0x800CECA0` to `0x800CECBC`). From 0 for each piece. |
| Timer speed | `+0x3254` (float) | Set per episode in Story, read live: **1, 2, 2, 2, 3, 3, 3, 6, 6, 6** for Episodes 1 to 10. That is 60, 120, 180 or 360 a second. The level start sets 1.0 (`0x800AF00C`). Another switch (`0x800CF508`, jump table at `0x800F1140`) sets it from `+0x2B0C` ÷ 3,600, which is whole minutes of a clock held there (Time Trial's counts down from about 5:01). It gives 1 for 0 or 1 minutes, 2 for 2 or 3, 3 for 4 to 9 and 6 for 10 or more, and it runs only when `0x8015D980` is 2 or more. Which mode uses it, and which code sets Story's values, is not pinned yet. |
| HUD form | `+0x2B10` | Timer × 600 ÷ the limit (`0x800F1168`, 6,000), 0 to 600. Warning sounds at 189 and 389. |
| Warnings | `+0x2404` | When the 0 to 600 form reaches **200** (first warning), the fall's speed is set to 25 (16.16 fixed point), with a whoosh; at **400** (second warning), to 75. |
| Fall | `+0x2414` | Gains that speed each step while it is at most 30,845 (the double at `0x800F10B8`), at `0x800AD7D4`. It starts at **17,050** for a stage's first piece and at about 20,300 to 20,600 for later ones (the later value is not yet explained). |
| Eased fall | `+0x2416` | Gains (fall − eased) ÷ 9 + 1 each step; the camera's distance follows it (`0x800CEF00`). |
| The drop | `0x800AD58C` | When the eased fall reaches **30,690** (the double at `0x800F10B0`). Also when the timer reaches 6,000. |

## What this means in play

The piece's whole time, and when its warnings come (worked out with `drop_run`):

| Episodes | Timer speed | Whole time | First warning | Second warning |
| --- | --- | --- | --- | --- |
| 1 | 1 | 42.5 s (first piece), 40.3 s (later) | 33.3 s | never: the fall ends first |
| 2 to 4 | 2 | 25.9 s, 23.6 s | 16.7 s | never |
| 5 to 7 | 3 | 20.3 s, 18.1 s | 11.1 s | never |
| 8 to 10 | 6 | 12.5 s, 11.7 s | 5.6 s | 11.1 s (about 1 s before the drop) |

So in Episodes 1 to 7 a piece has a long calm stretch and then 7 to 9 seconds of falling, with one warning. Only Episodes 8 to 10 reach the second warning. The fall itself always takes about the same time, because only the timer's speed changes by episode. A stage's first piece gets about 2 seconds more.

## The HUD's bar

The bar is the time left to the drop: `drop_run` steps through the rules above from the current values. It was checked against forced drops in Rescue 1:1 and 10:10, where it empties 0.15 to 0.2 s before the heart is lost (the piece's drop itself). Its notches are at the two warnings, worked out for each piece. It is green until the first, amber until the second and red after it, so each warning sound plays as the gauge crosses its notch.

## Research switches

- `BLOCKPLANET_DROP_LOG=1`: logs the timer, its speed, the steps per frame, the fall, the eased fall and the hearts whenever they change. It also logs the column under the cursor and the HUD bar's value.
- `BLOCKPLANET_DROP_TEST_KEEP=n`: empties every layer above n in a 5 × 5 patch round the cursor each time a piece comes into hand.
