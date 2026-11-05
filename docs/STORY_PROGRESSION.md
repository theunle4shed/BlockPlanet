# Story progression (BlockPlanet)

What happens after each stage ("episode" in the game) is completed: the cutscene, the card shown before the next stage, and what the original unlocks. The **BlockPlanet plan** column is where new rewards (modes, characters, tracks, options) get placed; it is empty until decided. Research and addresses: [research/blockplanet-game-state.md](research/blockplanet-game-state.md#between-chapter-scenes-endings-and-credits-2026-11-05).

**Seen** marks rows watched in play (2026-11-05); the others come from the game's code and text. Every stage has 10 levels.

## Story (Rescue): 10 stages

After every stage except the two endings, the game shows a **Continue / Exit** screen with the robot rescued in that stage, then an **"EPISODE n"** card with a message, then the next stage starts.

| Completed | Cutscene | Card before the next stage | Original unlock | Seen | BlockPlanet plan |
| --- | --- | --- | --- | --- | --- |
| 1 | Continue / Exit with the rescued robot (round, brown) | **Episode 2:** "Congratulations you passed the first episode. Ready to try the next one?" | Stage 2 | ✓ | |
| 2 | Continue / Exit with the rescued robot | **Episode 3:** "Episode 2 down. It's not always going to be this easy." | Stage 3 | | |
| 3 | Continue / Exit with the rescued robot | **Episode 4:** "Great! Have you tried gravity combos??? Keep playing for the victory." | Stage 4 | | |
| 4 | Continue / Exit with the rescued robot | **Episode 5:** "Almost there! Beat this for a great ending!" | Stage 5 | | |
| **5** | **First ending:** the rescued robots together on a planet; the credits are believed to follow (not yet captured) | **Episode 6:** "Still with me? There is an even greater reward ahead, but it gets real hard from here! Only fuse combos can help get your X count above 10" | Stages 6–8 (the hard stages) | ✓ | |
| 6 | Continue / Exit with the rescued robot | **Episode 7:** "If you do any gravity combo or fuse combo, it will triple your time return on power pieces! Finishing the bonus episodes will give you a code." | Stage 7 | | |
| 7 | Continue / Exit with the rescued robot | **Episode 8:** "Final victory! Beat this and you will get the final victory, and access to the bonus episodes!" | Stage 8 | | |
| **8** | **Final victory:** the characters on a theatre stage with curtains | **Episode 9:** "Bonus episode. Only true Tetrisphere champions should try this episode. Did you know you can create power pieces with your shadow?? If you are tricky you can even do it with a wildcard." | **Bonus stages 9–10** | ✓ | |
| 9 | Continue / Exit with the rescued robot | **Episode 10:** "That was amazing! Enter your name as LINES for a hidden game! The ultimate challenge of episode 10 awaits." | Stage 10; tells the player how to unlock **Lines** (name LINES) | | |
| 10 | Not yet known (no further card; the last stage) | — | "A code" (promised in the stage 7 card; not yet found) | | |

Notes:

- **Lines** is reached in the original by entering the name LINES, not by completing a stage; BlockPlanet already offers it in the menus from the start (decided earlier).
- The robots: the profile characters (Rocket, Gear, Gyro, Stomp, Wheels, Turbine) are six; which robot each stage rescues is not yet recorded.
- After a game over, Try Again restarts the stage from its first level (kept as the original).

## Other modes (from the code; not yet seen in play)

| Mode | Stages | Cards (after stages 1–4) | Endings |
| --- | --- | --- | --- |
| Hide & Seek | 30 levels per stage | 1 "…you finished the first episode. The real challenge awaits you…" · 2 "Now you understand. Have you tried advanced training? Mastering the 'X' technique is vital…" · 3 "…you've made it this far!… Completing the rest of the episodes will really be a challenge. To see who we are anytime enter your name as CR…" · 4 "O.K. now you're getting good! But this final challenge awaits… try pressing L, C-right and C-down… when entering your name." | After stage 3 |
| Lines | 10 levels per stage | 1 "…passed the first episode…" · 2 "Episode 2 down…" · 3 "Almost there, now it gets real tough." · 4 "Way to go!! Try entering +;<>^ as your name for access to any level!" | After stages 3 and 5 |
| VS CPU | 7 rounds per stage | 1 "Congratulations, but we're just getting warmed up." · 2 "Great! Finish this for a victory!" · 3 "Good job! You've beaten us. Now you can challenge the master." · 4 "Beat this episode to be the ultimate Tetrisphere champion!" | After stages 3 and 5 |

## Original cheats mentioned in the cards

| Name or input | Effect (as the cards describe it) |
| --- | --- |
| LINES | A hidden game (Lines) |
| +;<>^ | Access to any level |
| CR… | Shows who made the game (the credits) |
| L + C-right + C-down at name entry | "Just for fun" (effect not yet known) |

## Points to decide

- Which new rewards go where (modes, profile characters and colours, music tracks, options), and whether the original unlocks stay where they are.
- Whether the cards keep the original text, are rewritten, or point to BlockPlanet's own features instead of the old name cheats.
- Whether the cutscenes stay as they are or are replaced, once the art direction is set.
