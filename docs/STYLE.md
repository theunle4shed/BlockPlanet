# BlockPlanet style (decided 2026-11-08)

**Status: decided by the owner on 2026-11-08 (D1 to D10 at the end, all as recommended) and applied** to `assets/ui/menu.rcss`, `assets/ui/hud.rcss`, `assets/ui/hud.rml` and the styles set in code (`src/ui/ui_system.cpp`, `src/platform/linux_main.cpp`). New screens and HUD parts use these roles; a place that needs something else is added here first.

One style for menus, the HUD and cards. All sizes are in dp: a screen 1080 pixels high has 1 dp = 1 pixel. The Text size setting multiplies all of them (D1).

## How this review was made

The game could not be built or run for this review. The session had no ROM and no generated code, so CMake stops at `TETRISPHERE_GENERATED_DIR`. The captures were made a different way. `tools/qa/style/render.py` converts each RML screen and its RCSS into HTML, with dp turned into pixels, RmlUi's `font-effect` turned into text shadows, and sample data in place of the lists the game fills at run time. Chromium then renders each page with the game's Lato fonts. The HUD and card captures are hand-built pages that use the same markup as `src/ui/ui_system.cpp`, with sample values. These captures are close to the game but not identical. See "What was not captured". The same tool now renders the applied style sheets (`BLOCKPLANET_TEXT_SCALE=1.4` for Larger text); the result still has to be checked in the game itself.

## Text roles

| Role | Size | Weight | Colour | Used for |
|---|---|---|---|---|
| Hero | 96 | bold, letter-spacing 4 | White | Paused, How to Play |
| Display | 64 | bold | White | screen titles; HUD stage number and counts |
| Figure | 44 | bold | White (Gold when live) | HUD numbers that change: time, clock, combo, points popping up, Versus counts |
| Card | 44 | regular italic; headings bold | Orange; headings Yellow; terms Term | card and speech text (one size for every card) |
| Item | 40 (34 in long lists) | regular | Orange; focused White | menu choices; HUD score (bold) |
| Body | 26 | regular | Soft (menus), White (HUD) | descriptions, paragraphs, subtitles (Yellow), HUD goal and difference, card hints |
| Note | 24 | regular | Yellow | notes beside items, values, hints, the version, HUD mode name, item name, popup note, title notice |
| Small | 20 | regular, or bold for labels | Muted | HUD labels and sublabels, splits, gains, best, combo extras, the disclaimer |

Nothing is smaller than Small (20), except the title notice's one-line portrait layout. Special cases that stay: the name being typed (56) and the on-screen keyboard (34, wide keys 26).

## Colours

| Token | Value | Use |
|---|---|---|
| White | `#F8F8F8` | focused item, titles, HUD values |
| Orange | `#F5821F` | menu items, card body, item names |
| Yellow | `#E8B818` | subtitles, notes, values, hints, card headings, Training's introduced part |
| Gold | `#FFD23C` | live game feedback only: points popping up, a combo building, magic |
| Term | `#86D8FF` | game terms in cards and speech (glowing where first introduced) |
| Soft | `#C8D0E8` | descriptions, paragraphs, best times, splits |
| Muted | `#9AA6C8` | labels, older gains, the version, the disclaimer |
| Green / Red | `#5AD46A` / `#F05A4A` | ahead / behind, progress fill / a low clock or drop timer |

Bar fills keep their own colours (drop `#F5C21F`, magic `#9C5CF0`, default `#38C8F0`), and so do hearts (`#E02838`) and the title notice (`#4FB3D0`). These merge into the tokens above: `#F2C14E`, `#FFD84A`, `#FFC61A`, `#FFE08A` (into Yellow or Gold), `#DFE3EE`, `#C8CCD8` (into Soft), `#A8B0C8` (into Muted), `#48D060` (into Green), `#F04030` (into Red), and the cards' `#FF8C1A` and `#FFD23C` (into Orange and Yellow, D3).

## Panels

| | Fill | Rim | Radius | Padding |
|---|---|---|---|---|
| Menu panel | `#0B1020` at 85% (`D8`) | 3 `#F0F0F0` | 24 | 48 (long lists 40 48) |
| Card panel | `#0B1020` at 94% (`F0`) | 3 `#F0F0F0` | 24 | in em, so it follows the text |
| HUD container | `#0B1020` at 75% (`C0`, D8) | none | 24 | 16 24 20 |
| Inside a panel (description, name field, keys) | white at 6% (`#FFFFFF10`) | none | 14 | 16 28 |
| Focused item | white at 14% (`#FFFFFF24`) | none | 14 | 10 28 |

Bars have radius 8 and hearts radius 12. The card panel moves from vh to dp, so the Text size setting scales it the same way as everything else.

## Spacing

Steps of 4, 8, 12, 16, 24, 32 and 48. Item text, section text and description text all start 28 in from the panel's inner edge, so they line up.

## Outlines

- **Inside menus and cards: none.** The text sits on a near-opaque panel (D4).
- **HUD:** 1 dp on values over the tint, none on labels.
- **Text drawn straight onto the game** (the game's own menus, points popping up, the title notice): 2 dp `#1A0C04` (popups 3 dp).

## Focus

The focused item turns White and gets the stronger pill. It keeps its regular weight, so the label and its note no longer shift sideways when focus moves (D5).

## Numbers

- Thousands with commas: `1,284,550`.
- Times as `m:ss.cc`: `6:41.20`. Differences are signed, with a real minus: `+0:03.12`, `−0:01.40`.
- Multiplier: D9.
- Lato's figures are proportional: a 1 is about 5% narrower than a 0, so a running clock or score jiggles. RmlUi has no `tabular-nums`, so live clocks and the score get one fixed-width box per digit, as the splits already have.

## Hints

A button symbol, then one word in sentence case: `[A] Next`, `[A] Start`. Hints are Yellow and regular weight: Note size in menus, Body size on cards. A card's hint was grey and bold, and becomes Yellow like the menus' hints.

## What changes, place by place

| Place | Now | Becomes |
|---|---|---|
| `.header` (Paused) | letter-spacing 12 | 4 |
| `#panel` | radius 28, padding 48 56, `D0` | radius 24, padding 48, `D8` |
| `body` (menus) | 2 dp outline on all text | none |
| `.item.focused` | bold, pill `1A` | regular, pill `24` |
| `.item .note`, `.tiles .note`, `.name-field-hint`, `.version` | 22 | 24 (version Muted) |
| `.section`, `.hint` | unused | removed, or kept as Small bold Muted and Note Yellow |
| `.alt-hint` | `#F2C14E` | Yellow |
| `.howto .para` | `#DFE3EE` | Soft |
| HUD `.label`, `.sublabel`, `.versus .label` | 19, 17, 17 | Small 20 bold |
| `#combo-magic`, `#combo-best`, `.gain` | 18 | 20 |
| `#best` | 22 | 20 |
| `#score` | 36 | Item 40 bold |
| `#combo` | 40 | Figure 44 |
| `#clock` | 46 | Figure 44 |
| `.random-block` | 48 | Figure 44 |
| `#where .label`, `#item .name` | 22 | Note 24 |
| `.popup .points` | `#FFD84A` | Gold |
| combo when magic | `#FFC61A` | Gold, with the glow |
| `#progress .fill`, hurry fills | `#48D060`, `#F04030` | Green, Red |
| `.group` tint | `B0` | `C0` |
| Side radius | 22 | 24 |
| Title disclaimer | 17 | 20 |
| `.card-panel` | `#0D1222`, rim 0.35vh, radius 2.2vh, size 10/240 of the frame (rules cards ×1.1) | `#0B1020F0`, rim 3, radius 24, Card 44 (one size) |
| `.card-heading` | 1.12em, 1 dp outline, `#FFD23C` | 1em, no outline, Yellow |
| card body | `#FF8C1A`, 2 dp outline (inherited) | Orange, no outline |
| `.card-hint` | 0.78em bold `#C8CCD8` | Body (0.6em), regular, Yellow |
| Training's introduced part | `#F2C14E40`, label `#FFE08A` | Yellow at 22%, label Yellow |
| Portrait HUD sizes | 40, 48, 32, 18, 22 | Item 40, Figure 44, Item 34, Small 20, Note 24 |

## Found along the way

- **The Text size setting only reaches cards.** Menus and the HUD size dp from the window height alone (`ui_system.cpp`, `SetDensityIndependentPixelRatio(height / 1080)`); cards multiply by `card_text_scale()` in `linux_main.cpp`. To make it reach everything, multiply that ratio by the same scale (D1). At Larger (1.4) the main menu (about 890 dp tall) no longer fits 1080 and needs its list to scroll; at Large (1.2) it just fits.
- **The game's own menus jump sideways.** `publish_game_text()` lines up Try Again, Exit and Training on the smallest x of the current frame. The original slides those items, so that smallest x changes from frame to frame and the whole column moves with it. Versus' Continue and Exit are not handled at all. Fix: a fixed column (a set x in the game's 320 columns) for both menus, or BlockPlanet panels in their place (D10).
- **Game over and Versus' end:** the HUD stays up. It should fade out over about 0.2 s when either menu appears.
- **Applied, still to check in the game:** the end menus' column is held at the leftmost place seen since the menu appeared. Game over's items are matched as Try Again, Exit and Training; Versus' items are assumed to be Continue and Exit, which no capture has confirmed. The game's own text (its end menus) keeps the game's size and is not scaled by Text size, so it stays fitted to the game's layout. At Larger, the menu panels scroll (at most 90% of the window's height), and the HUD panels grow by 40%. `place_hud_sides` keeps them on screen, but on 4:3 windows they may cover more of the sphere.
- **Lists:** the chapter and stage lists scroll in compact rows. The pause menu (up to six items) and the profile list fit at every size captured. The stage list (11 rows) scrolls at 16:9 and 4:3.

## What was not captured

- Anything drawn by the game itself: the sphere, the magic's 3D model, the original screens behind the menus. A plain gradient stands in for them.
- The real positions of the HUD panels and cards. `ui_system.cpp` places them from the game's frame; here they are placed by hand.
- The game's own screens (game over, Versus' end) as the game draws them: their animation, the sideways jump, the exact positions. The game over capture is a mock-up.
- The Puzzle, Lines, Hide & Seek and Time Trial variants of the HUD (only Story, Time Attack and Versus are mocked), Training's full row, and portrait layouts.
- The Training list as its own screen (it uses the chapter list's layout, which was captured).
- Text sizes other than Default, scrolling, transitions, and RmlUi's exact text rendering (its outline and line height differ slightly from Chromium's).
- Windows.

## Decisions (the owner, 2026-11-08)

All ten were decided as recommended:


- **D1:** Should Text size scale menus and the HUD too, not just cards? Recommended: yes, with the main menu scrolling at Larger.
- **D2:** One card size, 44, for speech and rules cards, with headings the same size, bold and Yellow? Now it is about 45 for speech, about 50 for rules cards, and headings 1.12×.
- **D3:** Card colours to match the menus (Orange `#F5821F`, Yellow `#E8B818`) instead of the game's `#FF8C1A` and `#FFD23C`? Recommended: yes.
- **D4:** No outline on text inside menus and cards? Recommended: yes.
- **D5:** Should the focused item stay regular weight, with a stronger pill? Recommended: yes.
- **D6:** Note 22 → 24, and Small 20 as the smallest HUD text (now 17 to 19)? Recommended: yes.
- **D7:** One Gold (`#FFD23C`) for all live feedback? Recommended: yes.
- **D8:** HUD tint `B0` → `C0`, with a 1 dp outline on values instead of 2? Recommended: yes.
- **D9:** Multiplier as `3x` (now), `3×` or `×3`? Recommended: `×3`.
- **D10:** The game's own end menus: a steady column now and BlockPlanet panels later, or panels now? Recommended: a steady column now.
