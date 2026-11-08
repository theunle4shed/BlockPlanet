# Card text audit (2026-11-07)

The game's cards were read from the ROM, every one: the Training lessons, Hide & Seek's goals, the Lines and VS rules, and the messages between Story's episodes. This page lists only notes and short fragments for finding each card. The texts themselves stay in the player's ROM (AGENTS.md). Each card is plain text in the game's code segment (ROM `0xC83FC`–`0xCB4D0`), with colour codes `-0` (orange) and `-1` (yellow) and page and pause codes `-2` to `-5` (Training's pages, and its waits for a demonstration).

## Codes, now handled

| Code | Meaning | Shown as |
|---|---|---|
| `%A%`, `%B%` | A, B | The player's confirm and cancel buttons |
| `%L%`, `%R%` | L, R (zoom in Puzzle) | Bumpers: LB / RB, L1 / R1, L / R; keys J and S |
| `%C#%` | C-down (Magic) | The player's Magic button |
| `%C"%` | C-up (restart a puzzle) | The player's C-up button (Y, Triangle, X; key I) |
| `%C$%` | C-right | Right trigger: RT, R2, ZR; key L |
| `%` elsewhere | The font's apostrophe and quote mark ("let%s", "your %X% count") | ’ and ‘X’ |

Glyph evidence for the C buttons: `docs/research/m2-native-prompts.md`. An earlier `%Z%` entry was a mistake (no card uses it) and has been removed.

## Fixed in presentation

- **Typewriter spacing**, a space before ! and ? ("good !", "distance ??"), is removed.
- **Case**: names keep their capitals (Wheels, Turbine, Tetrisphere, Nintendo), and so does "I", including "I’m".
- **Puzzle's drops and slides counters** use the cards' font. They were being read as card text; they are now kept apart.
- **Training's box** sits right of centre, so the wrap width now comes from the game's own line ends instead of assuming a centred box.

## For the owner to decide

1. **Name-entry codes no longer work.** BlockPlanet uses profiles, not name entry. Four episode messages tell the player to enter a name: one for the credits, one for the hidden Lines game, one for a level-select code, and one for an L + C-right + C-down press while entering a name. In BlockPlanet, Lines and Credits are in the menus and every stage reached can be picked, so these hints point at nothing. Suggestion: replace each with BlockPlanet's own line saying what is now unlocked (for example "Lines is now in your Solo menu").
2. **Doubled punctuation**: "??" and "!!" appear in three cards. Suggestion: single marks.
3. **Spelling and grammar**: "tallys" (tallies), "less than 20 pieces" (fewer than), "can not" (cannot).
4. **"Ball" or "sphere"**: the cards say "ball" throughout, while BlockPlanet's menus say sphere. Suggestion: "sphere" in BlockPlanet's panel text.
5. **N64 wording**: the cards mention the "control pad" and "page through with down or A". With BlockPlanet's panel showing all of a card at once, the paging line can go. "Control pad" could become the stick, d-pad or arrow keys, depending on the player's input.
6. **Hide & Seek headings** are internal names: Multi, Drill, Hide and Seek, Shift, Unique, Brick, Connect, Tower, Crystal Tower. Suggested panel titles: Any Picture, Five Drills, Help Them Escape, Four Parts, Unique Shape, Brick Drop, Connected Pictures, Tower, Crystal Tower.
7. **Episode messages have no heading.** Suggestion: "Episode N Complete", over the game's message.
8. **"Then you can capture him"**: the Hide & Seek "friend" is a robot. The wording is fine as it is; noted only in case the robots get named on the card.

## Still in pictures

- The game-over hint and the short Hide & Seek hints ("look under the towers…", "find all 4 parts…", "uncover multiple cores…") are painted into pictures in the main archive (around ROM `0x7D5C24`), not drawn as text.
- So are the "▼ more ▼" and "◀ watch" prompts under the cards. Training's are now replaced (below).

The others stay as pictures until BlockPlanet's card panel replaces them.

## Training's bubble (2026-11-07)

Training's speech bubble is a 150 × 115 one-byte image drawn by the image helper `0x8003396C` at (154, 15). Its body runs from x 155 to 300 and y 15 to 113, and its tail at x 272–283 reaches y 122. The prompts under it are "▼ more ▼" (62 × 14 at (201, 115)) and "◀ watch" (50 × 14 at (200, 116)). BlockPlanet hides the three in Training (mode 3) and draws its own panel in their place. Hide & Seek's and Lines' boxes are other pictures, still drawn by the game.

## Rewrite review (2026-11-07)

The owner chose to review only the lines Claude would change, rewritten freely in BlockPlanet's voice and never wordy. The review is a private page for the owner (not in the repository, as it quotes the game's text from the owner's ROM). It covers 58 lines: Training (Basics, Advanced, Puzzle), Hide & Seek's goals, the Lines and VS rules, and the game-over hint. For each, the owner chooses: use the suggestion, keep the original (with the fixes already applied), or their own wording. The decisions are then applied as replacements keyed by each line's place in the game's text. Open point raised there: BlockPlanet's HUD says "Health" where the game says "lives". The suggestion is to label the HUD "Lives".

Applied 2026-11-08 (`text_rewrites.cpp`). Points from the owner's notes:
- **"Combine with next/previous" (the fuse pages):** the two lines sit either side of a demonstration and a scripted arrow, so they stay separate pages. Each is shortened so they read on together.
- **"Where does the combo counter show?":** by the sphere, where the combo happens. The rewrite says so.
- **"Mention where the magic appears":** "in the Magic box at the bottom of the screen" (Training's HUD row).
- **"Do we need to explain power bricks, or give a demo?":** the lesson already demonstrates a power combo right after. A "your turn" step for it could follow.
- **"Is zoom true for every mode?":** zoom is explained in Puzzle Training. Whether it works outside Puzzle is still to check.
