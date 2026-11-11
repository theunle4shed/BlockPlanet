# Tetrisphere cheat codes, and which could become BlockPlanet modifiers

Research date: 2026-11-11. The owner's question: were there popular cheat-device codes that added fun mechanics, which BlockPlanet could offer as features or modifiers? Tetrisphere is an N64 game, so the devices are GameShark, Action Replay and Xploder (Game Genie never came to the N64).

Companion to [fan-feedback.md](fan-feedback.md). Addresses are checked against BlockPlanet's own memory map in [blockplanet-game-state.md](blockplanet-game-state.md) (US ROM, NTPE rev 0).

## Limits

- **Pages could not be opened.** As for the fan research, this session's network policy blocked fetching pages, so the codes come from search-engine summaries of the cheat sites. Codes that two or more sites give the same way are marked as agreed; the rest are *single source*.
- **No ROM or generated code in this session,** so no code was tested in the game. The "What it changes" column is reasoning from BlockPlanet's memory map; the codes that patch the game's own instructions need checking against the generated code.
- **Little fan discussion.** Searches found the codes on many cheat sites, but almost no forum or video posts about which codes were fun (Reddit could not be searched). The name codes (LINES, level select, VORTEX, the secret music) are what players actually talk about.

## How N64 cheat codes work (for reading the tables)

- `80aaaaaa 00vv` writes the byte `vv` to address `0x80aaaaaa`, again and again while the game runs.
- `81aaaaaa vvvv` writes the halfword `vvvv` (16 bits).
- `D0aaaaaa 00vv` is an *activator*: the code on the next line runs only while the byte at that address equals `vv` (usually a controller button).
- When the address is in the game's code rather than its data, the write changes an instruction. `2400` written over the top half of a MIPS instruction turns it into one that does nothing (it writes to the zero register), so a code like `81094B44 2400` removes one instruction of the game.

## The name codes (no device needed)

Typed as a name under NEW NAME. Secret symbols are reached by holding L + C-right + C-down. BlockPlanet found the table at `0x800E12D8`: a string pointer and a match count for each name (credits example: CREDITS sets `0x800E12EC` = 1).

| Name | What it does | Sources | BlockPlanet |
| --- | --- | --- | --- |
| LINES | Unlocks the hidden Lines game (dig a path through the core for a friend to escape; no dropping, always a wild piece). Has to be typed again each session. | [GameRevolution](https://www.gamerevolution.com/?p=28996), [Supercheats](https://www.supercheats.com/nintendo64/tetrisphere/6109/unlockables/), [Cheat Happens](https://www.cheathappens.com/4121-Nintendo_64-Tetrisphere_cheats) | Done: Lines is available from the start. |
| Saturn, ship, rocket, heart, skull (`+;<>^` in the table) | Any level in any mode. | [Cyberly](https://www.cyberly.org/game-cheats/nintendo-64-n64/tetrisphere.html), [Nintendo64EVER](https://nintendo64ever.com/Cheats-Nintendo-64-Game,342,Tetrisphere,1.html), [GameFAQs guide by M. Stoler](https://gamefaqs.gamespot.com/n64/198946-tetrisphere/faqs/3332) | Planned as level select by chapter; `--unlock-all` for testing. |
| CREDITS | Shows the credits. | Same sources | Done: Credits on the main menu. |
| G + alien head + MEBOY | Unlocks nine extra songs on the Audio screen. The cheat sites' "GameBoy cheat message" probably refers to this name (a play on "Game Boy"). | [CheatCC](https://www.cheatcc.com/?p=84010), [GameFAQs cheats](https://gamefaqs.gamespot.com/n64/198946-tetrisphere/cheats) | Planned: the special music in the jukebox. |
| VORTEX | Hold Reset after entering it: after a few seconds a looping scene of all the robots sucked into a vortex plays for as long as Reset is held. | [CheatCC](https://www.cheatcc.com/?p=84010), [GameFAQs cheats (Baseman)](https://gamefaqs.gamespot.com/n64/198946-tetrisphere/cheats) | **New:** a PC has no Reset button, so the scene is unreachable unless BlockPlanet offers it (an Extras item, or a jukebox visual). |

Players wanted these unlocks to stay: a romhacking.net patch makes Lines and level select permanent ([Romhacking.net #8487](https://www.romhacking.net/hacks/8487/); also [gallaux/NintendoRomHacks](https://github.com/gallaux/NintendoRomHacks)). There is also a RetroAchievements set for the game ([RetroAchievements](https://retroachievements.org/game/10172)), which shows players enjoy goals beyond the story.

## GameShark and Action Replay codes (US version)

Player 1's context starts at `0x80110220` and player 2's at `0x80113488`, `0x3268` apart. Every per-player code below is exactly that distance apart for P1 and P2, which supports the sources and the map.

| Code | Name on the cheat sites | What it changes (BlockPlanet's map) | Agreement | Sources |
| --- | --- | --- | --- | --- |
| `80112F9F 0003` | Infinite misses, P1 | Hearts left: the low byte of context `+0x2D7E` (`0x80112F9E`), held at 3. | Agreed (several sites, Codejunkies) | [Almar's Guides](https://almarsguides.com/retro/walkthroughs/N64/Games/Tetrisphere/Gameshark/SingleandVSModeCodes/), [CheatCC](https://www.cheatcc.com/articles/tetrisphere-cheats-codes-cheat-codes-for-nintendo-64-n64-n64/), [Codejunkies](http://us.codejunkies.com/search/codes/Tetrisphere_Nintendo-64_6295981-6___.aspx), [Ethereal Games](https://etherealgames.com/n64/t/tetrisphere/action-replay-codes-us/) |
| `80116207 0003` | Infinite misses, P2 | P2's hearts (`0x80113488 + 0x2D7F`). | Agreed | [Almar's Guides](https://almarsguides.com/retro/walkthroughs/N64/Games/Tetrisphere/Gameshark/SingleandVSModeCodes/) |
| `80112F9E 0003` | "GameBoy cheat message", single player | The high byte of the same hearts value. Writing 3 there would make hearts 0x03xx, so this is probably a mislabelled or mistyped entry. | Listed, effect unclear; the summary did not name the site | Search summary only |
| `80110922 00vv` | Weapon modifier, P1 | The held magic, context `+0x702`: −1 none, 0 to 5 the magic (see below). | Agreed | [Total Cheats](http://www.total-cheats.com/n64/tetrisphere.html), [CheatCC](https://www.cheatcc.com/articles/tetrisphere-cheats-codes-cheat-codes-for-nintendo-64-n64-n64/), [GameGenie.com](https://gamegenie.com/cheats/gameshark/n64/tetrisphere.html) |
| `80113B8A 00vv` | Weapon modifier, P2 | P2's held magic. In VS one source says you always get a rocket. | Agreed | Same |
| `D0102590 00??`, `D0102591 00??` | Activators 1 and 2, P1 | Button conditions (probably P1's controller buttons at `0x80102590`); the button values were not in the summaries. | Single source, incomplete | [Almar's Guides](https://almarsguides.com/retro/walkthroughs/N64/Games/Tetrisphere/Gameshark/) |
| `80112629 0000` | Infinite time, P1 | Context `+0x2409`, which BlockPlanet has not identified. It is not the drop timer (`+0x2B10`) or Time Trial's clock (`+0x2B0C`). | Agreed (two sites) | [CheatCC](https://www.cheatcc.com/articles/tetrisphere-cheats-codes-cheat-codes-for-nintendo-64-n64-n64/), [Almar's Guides](https://almarsguides.com/retro/walkthroughs/N64/Games/Tetrisphere/Gameshark/SingleandVSModeCodes/) |
| `80115890 0000` | Infinite time, P2 | P2's `+0x2408`: one byte off from P1's `+0x2409`, so one of the two is probably mistyped (a 16-bit value's high and low bytes). | Single source | [Almar's Guides](https://almarsguides.com/retro/walkthroughs/N64/Games/Tetrisphere/Gameshark/SingleandVSModeCodes/) |
| `810CECBC 2400` | Infinite time, both players | Patches the instruction at `0x800CECBC` into a do-nothing: probably the line that advances a timer. | Single source | [Almar's Guides](https://almarsguides.com/retro/walkthroughs/N64/Games/Tetrisphere/Gameshark/) |
| `81094B44 2400` | Never miss | Patches the instruction at `0x80094B44`: probably the heart loss on a bad drop. | Single source | [Almar's Guides](https://almarsguides.com/retro/walkthroughs/N64/Games/Tetrisphere/Gameshark/) |
| `8011265F 00AD` | Secret cheat menu screen | Context `+0x243F`, unknown to BlockPlanet. No source says what the screen shows. | Agreed (several sites, Codejunkies) | [Almar's Guides](https://almarsguides.com/retro/walkthroughs/N64/Games/Tetrisphere/Gameshark/SingleandVSModeCodes/), [Codejunkies](http://us.codejunkies.com/search/codes/Tetrisphere_Nintendo-64_6295981-6___.aspx), [Cheatmasters](https://w.cheatmasters.com/n64/tetis_p.html), [GameGenie.com](https://gamegenie.com/cheats/gameshark/n64/tetrisphere.html) |
| `80112D18 vvvv`, `80112D1A vvvv` | Score modifier P1 (smaller, larger) | The score, context `+0x2AF8` (u32, `0x80112D18`): its high and low halves. ("Replace the X's with F's for a quick score gain".) | Agreed | [Total Cheats](http://www.total-cheats.com/n64/tetrisphere.html), [GameGenie.com](https://gamegenie.com/cheats/gameshark/n64/tetrisphere.html), [CheatCC](https://www.cheatcc.com/articles/tetrisphere-cheats-codes-cheat-codes-for-nintendo-64-n64-n64/) |
| `810E12DC 0001` | Stage select | The match count of the level-select name (`+;<>^`, the table entry at `0x800E12D8`). | Single source, but matches the table exactly | [CodeTwink](http://www.codetwink.com/view-cheats-1641-title-tetrisphere.html) |
| `810E12E4 0001` | Unlock Lines | The match count of LINES, the next entry. | Single source, but matches the table | [CodeTwink](http://www.codetwink.com/view-cheats-1641-title-tetrisphere.html) |

**Weapon values** (cheat sites' names, then BlockPlanet's names from the manual): 00 rocket (Firecracker), 01 dynamite (Bundle O' Dynamite), 02 magnet (Electro Magnet), 03 three purple bars (Atom), 04 drop bomb (Bomb), 05 green spike ball (Ray Gun). The order matches BlockPlanet's upgrade order. The pairing of the cheat sites' descriptions with the manual's names is an inference.

### A second set, probably another version

[Kai666](https://www.kai666.com/cheat-codes-n64/tetrisphere.htm) lists Xplorer and Action Replay codes at other addresses: unlimited time `80114EB9 0000`, misses don't count `8011582F 0003`, secret cheat menu `80114EEF 00AD`, all levels `800E3D45 0001`. The first three are each exactly `0x2890` above the US codes, so they are the same variables shifted, probably for the European version (*unverified*). [GameHacking.org](https://gamehacking.org/game/20920) has a European entry (serial NTPP, CRC32 2E953131) with master codes by Mezmorize. No European code list was confirmed.

## The most promising modifiers

For each: what the code showed, how BlockPlanet could do it as an option, and where it fits. All of them should pause trophies (as Casual does now) and show a small "Modifiers" tag in the HUD and on records, unless the owner decides a modifier is part of a mode's rules.

| Modifier | From | How BlockPlanet could do it | Fit |
| --- | --- | --- | --- |
| **Start with magic, or choose your magic** | Weapon modifier | Set the held magic (`+0x702`) once at level start, or each time it is used, to the chosen magic; better, call the game's own give-magic path (`func_800C78A0`) so the sound and HUD follow. Options: Off, a random magic at each level, a chosen magic. | The most fun of the codes: big explosions on demand. A **Magic playground** in Custom or Casual, and a **Versus handicap** (the weaker player starts with magic), which REDESIGN.md already asks for. |
| **Relaxed drop timer** (slower, or off) | Infinite time | Find what `+0x2409` and the instruction at `0x800CECBC` really are. Then hook the timer's update and scale its step (0 %, 50 %, 100 %) instead of freezing it. | Matches the accessibility plan's "adjustable fall speed" and the most common complaint (too hard to learn). An **Assists** item. |
| **Forgiving drops** (a bad drop costs no heart) | Never miss | Hook the heart-loss path (probably near `0x80094B44`) and skip it for bad drops only, so the drop timer can still cost a heart. Show why the drop failed. | Learning aid, an Assists item; pairs with the cursor-clarity suggestion in fan-feedback.md. |
| **Infinite hearts** | Infinite misses | Already done as Casual (hearts refill, `src/platform/game_options.cpp`). | Done. Could also be a per-player Versus handicap (more hearts). |
| **Unlock everything** | Stage select, Lines | Done or planned in the menus (Lines from the start, level select by chapter, `--unlock-all`). | No modifier needed. |
| **Extra music** | G + alien + MEBOY | Find what the name sets, and offer the nine songs in the jukebox and music choice. | Planned; confirm the nine songs and whether they count among the 23 tracks already named. |
| **The vortex scene** | VORTEX | Find what the name and the held Reset start, and play the scene from an Extras item or as a jukebox visual. | New, small and delightful. |
| **The secret cheat menu** | `8011265F 00AD` | Research first: write `0xAD` to context `+0x243F` in a test build and record what appears. It may be a developer menu with more switches. | Research item; could feed the Modifiers menu. |
| **A piece of your choice** | Not a known code; came up in the question | BlockPlanet already knows the piece queue (context `+0x2374`, nine halfwords). An option could set the next piece, or bias the queue (more wild pieces, one shape only). | A Custom or training modifier, and a puzzle-making tool. |

Not worth offering: the score modifier (it only breaks records) and the activators (BlockPlanet has its own controls).

## How to turn a code into an option, not a memory write

1. **Find the code that changes the value.** A cheat writes a value every frame; BlockPlanet can instead hook the game function that changes it (give magic, lose a heart, step the timer) and decide there. The function hooks Blocksphere built for the widescreen HUD show the way.
2. **Where a write is still the simplest,** keep it in the game options layer (`src/platform/game_options.cpp`), which already refills hearts for Casual only while a game is being played and not in VS CPU, rather than writing blindly.
3. **One Modifiers screen,** grouped as Assists (drop timer, forgiving drops, infinite hearts) and Fun (magic, pieces). Each item has a one-line explanation, as Options already does. Saved per profile, and off for online play unless the host turns them on.
4. **Records and trophies stay honest:** modifiers pause trophies, and records made with modifiers are marked or kept separately.
5. **Verify each code first** in a build with the player's ROM: set the value or patch the instruction, watch the effect, and find the function it touches in the generated code. The single-source codes (never miss, both-players time, stage select, Lines) and the unknown addresses (`+0x2409`, `+0x243F`) need this most.
