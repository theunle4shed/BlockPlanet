# HUD inventory

What the in-game HUD shows, where and when, as reference for designing a replacement. Measured on 2026-11-01 from a BlockPlanet build of the US rev0 ROM: screenshots of every mode, plus a log of every textured rectangle RT64 drew in sampled frames. A visual reference sheet exists separately; captures of the game are not stored in Git.

## How to read this

- **Coordinates** are on the original 320×240 screen (4:3), x to the right and y down, as `(left, top)–(right, bottom)`. Multiply by 4 for a 1280×960 window, by 4.5 for 1440p, and so on.
- **Anchor** is the screen edge the element follows in 16:9: **Left**, **Right**, **Center bottom** or **Center** (text). Blocksphere's widescreen code moves Left and Right groups out to the edges of a wide screen. VS is not anchored yet; its 16:9 layout is unverified.
- **Native texture** is the size of the original image in the ROM. Replacement art can be larger. RT64 texture packs replace images by content, so new art does not need to match the original pixel size.
- The sphere, the falling pieces and the upcoming-piece queue are **3D models, not HUD images**. They are listed for layout purposes only.

## Single-player HUD (Rescue, Hide & Seek, Time Trial, Practice, Training)

Shown throughout gameplay. Hidden during pause, results, level intros and game over.

| Element | Where (4:3) | Anchor | Native texture | When and how it changes |
| --- | --- | --- | --- | --- |
| Mode label | (27,18)–(97,40); Time Trial (27,18)–(92,50) | Left | 70 px wide panel, drawn in strips | Always. Reads RESCUE, MULTI (Hide & Seek levels) or TIME TRIAL (two lines, which pushes the badge and hearts about 10 px lower). Practice shows the core count instead ("15 / CORE"). |
| Level badge (ring) | (27,40)–(97,76); Time Trial (27,50)–(92,89) | Left | shares the 70×70 and 65×58 panels | Always. Ring with the level number: "1:1" (chapter:level) in Rescue and Hide & Seek, "1" in Time Trial, core count in Practice. |
| Level digits | (42,42)–(67,59) | Left | 10×16 font glyphs | Updates when the level changes. |
| Hearts (lives) | (35,79)–(80,94); Time Trial (35,89)–(80,104) | Left | 15×14 heart, 15×15 skull | Three hearts. A lost life turns a heart into a skull, right to left. Not shown in Puzzle. |
| Heart backing | (27,78)–(87,103) | Left | 60×25 | Behind the hearts. |
| Upcoming pieces | left column, about x 20–60, y 110–220 | (3D) | models | Next three pieces, newest at the bottom. A "?" stands for a mystery piece. |
| Score | right-aligned, ending at (255,25)–(255,41) | Right | 10×16 font glyphs | Total score, updates on every scoring move. |
| Score pop-up | under the score, about (200,40)–(255,56) | Right | font glyphs | Briefly shows the points from the last move, for example "2 800". |
| Multiplier | (233,42)–(256,58) | Right | two 25×16 layers | "1x", "2x" and so on. Combo multiplier. |
| Magic ring | (263,27)–(293,57) | Right | 30×30 | Top of the magic gauge. Shows a counter during combos, for example "28". |
| Magic gauge trail | (264,59)–(292,155) | Right | 15 images, 27×7 growing to 27×27 | A trail of dots that grows in size downwards. Animates as magic charges. |
| Magic bowl | (234,154)–(299,231) | Right | 65×77 and 62×71 layers | Always. Holds the stored magic item. |
| Drop timer (was listed as the magic item) | (261,157)–(284,191) | Right | 23×34 and 20×31 | An orb in a brass casing whose dial turns blue, green, yellow while a piece is held; a heart is lost when it runs out (2026-11-05). Where magic is shown in the bowl is still to confirm. |
| Timer | (124,200)–(199,216) | Center bottom | 12×12 clock, font glyphs | Time Trial only. Counts down from about five minutes. |
| Message text | about (33,94)–(114,146) | Center | font glyphs | Short messages on top of the HUD, for example level start text. |

## Puzzle HUD

Puzzle has no hearts, piece queue or magic.

| Element | Where (4:3) | Anchor | Native texture | When |
| --- | --- | --- | --- | --- |
| Mode label | (27,18)–(95,40) | Left | 68 px wide panel | Always. Reads PUZZLE. |
| Level badge | (27,40)–(95,80) | Left | 68×62 and 65×61 | Always. Puzzle number. |
| Score | ending at (285,41) | Right | font glyphs | Always. |
| Multiplier | (263,42)–(286,58) | Right | two 25×16 layers | Always. |
| Drops counter | (254,128)–(294,174) | Right | 40×46 ring, label above | Drops used. |
| Slides counter | (254,173)–(294,219) | Right | 40×46 ring, label above | Slides used. |
| Timer | (124,200)–(199,216) | Center bottom | 12×12 clock, font glyphs | Counts up from 0:00:00. |

## VS HUD (VS CPU and two-player VS)

The screen is split down the middle with a sphere on each side. This layout is not anchored for widescreen yet.

| Element | Where (4:3) | Native texture | When |
| --- | --- | --- | --- |
| Center divider | (151,0)–(170,240) | 19 px strip | Always. |
| P1 magic bowl and item | (26,16)–(71,71) | 45×55 and 42×53 | Always. |
| P2 magic bowl and item | (255,16)–(302,71) | 45×55 and 42×53 | Always. |
| P1 magic gauge trail | (63,17)–(133,45) | 10 images, shrinking towards the center | Runs sideways, towards the center. |
| P2 magic gauge trail | (187,18)–(260,44) | mirrored | Mirrors P1. |
| Magic charge marks | along y 21–41, across both halves | 20 px marks | Charge state for each player. |
| Multipliers | (129,43)–(188,58) | two 25×16 | One per player, side by side at top center. |
| VS / core panel | (138,162)–(192,228) | 54×66 and 50×62 | Always. "VS" with each player's core count. |
| Core counts | (150,188)–(175,204) | font glyphs | Both players' cores, for example "4 4". |
| Hearts, both players | (120,175)–(205,219) | 15×15 heart and skull | Three per player, either side of the panel. |

## Overlays and events

These appear on top of gameplay at particular moments. Most are large text or 3D effects rather than HUD images, so their coordinates are approximate.

| Event | What appears | Where | When |
| --- | --- | --- | --- |
| Combo score | Large score, for example "3400", with a light burst | over the sphere, center | After a large combo. Fades after about a second. |
| Mystery piece | "?" in the piece queue | piece queue | When the next piece is a mystery piece. |
| Life lost | Heart turns into a skull | hearts | When a piece cannot be placed. |
| Magic launch | The item flies out of the bowl towards the sphere | bowl to sphere | When magic is used. |
| Level intro | Instruction box with mode name and goal, for example "MULTI: dig through the pieces…" | center | Start of Hide & Seek, Puzzle and Training levels. |
| Hide & Seek intro | Close fly-past of the sphere with the mode name ("MULTI") on its surface | full screen | Start of each Hide & Seek level. |
| Training text | Speech box with paging ("▼ more ▼"), controller picture and instructor | right half | Throughout Training. |
| VS intro card | "LEVEL 1, PLAYER 1 vs ROCKET" with both characters | full screen | Before each VS round. |
| VS round end | The losing sphere breaks apart | losing side | End of a VS round. |
| Game over | Large 3D "GAME OVER" | full screen | All hearts lost (Hide & Seek, Time Trial). |
| Retry menu | TRY AGAIN, EXIT, TRAINING, and the hint "You may want to try training if you haven't already" | center | After game over in Rescue. |
| Totals | Total score, highest combo score, biggest combo, X count, total pieces taken, total drops, cores uncovered, levels passed | full screen | End of a Time Trial run. |
| Pause | CONTINUE, SKIP, EXIT over the "paused" background, with the character | full screen | Start button during play. |

## Still to capture

- **Lines** (the hidden mode): its HUD has not been captured yet.
- **Two-player VS** with two controllers, compared with VS CPU.
- **Level complete and chapter complete** screens in Rescue.
- **Each magic type's** icon in the bowl and its launch effect.
- Exact layout in **16:9** for each element. Blocksphere moves the Left and Right groups to the screen edges; the amount depends on the window shape.

## Where it is drawn in code

For whoever replaces or moves elements later:

- `func_80075CD8` draws the single-player HUD: 87 draw calls, tagged Left, Right or Center in `config/recomp/ui_anchor_callsites.json`. Helper calls cover the multiplier, the score pop-up and the magic bowl model.
- `func_8003FE3C` draws the pause text.
- The drawing helpers it calls are at `0x8003396C`, `0x80071DE8`, `0x80038478` and `0x8007240C`. Their roles (image, digits, text) have not been confirmed one by one.
- The textured-rectangle log was produced with temporary local instrumentation in RT64's `RDP::drawTexRect`. It is not part of the build.
