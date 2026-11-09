# Menu layer (BlockPlanet)

BlockPlanet's new menus are built with [RmlUi](https://github.com/mikke89/RmlUi), an MIT-licensed library that lays out documents written in an HTML- and CSS-like language (RML and RCSS). BlockPlanet draws them over the game itself, through RT64.

RecompFrontend, the menu library other N64: Recompiled projects use, was not used: it has no licence, so BlockPlanet may not ship its code. Blocksphere kept it as a reference only. BlockPlanet's renderer was written independently.

## How it works

| Part | File | Role |
| --- | --- | --- |
| Renderer | `src/ui/ui_renderer.cpp` | Implements RmlUi's render interface on RT64's graphics layer (plume): geometry in host-visible buffers, textures uploaded through a staging buffer, scissor regions and transforms. Fonts and images become textures; PNG images are decoded with stb_image (compiled by RT64). |
| Shaders | `src/ui/shaders/UiVS.hlsl`, `UiPS.hlsl` | Pixels to clip space with RmlUi's transform; premultiplied-alpha colour times the texture. Compiled with RT64's DXC to SPIR-V (and DXIL on Windows). |
| System | `src/ui/ui_system.cpp` | Registers RT64's render hooks before RT64 starts, starts RmlUi when a menu first opens, applies queued requests and input on the presentation thread, and updates and draws the context while menus are open. |
| Navigator | `src/ui/ui_navigator.cpp` | Screens and focus (below). Uses RmlUi only, so it is unit-tested without RT64 (`tests/ui_navigator_test.cpp`). |
| Menu input | `src/platform/menu_input.cpp` | Turns SDL keyboard and controller events into menu actions while menus are open. |
| Assets | `assets/ui/` | Screens (`screens/*.rml`), the shared style sheet (`menu.rcss`) and fonts, copied beside the game into `ui/` at build time and included in the release package (`BLOCKPLANET_UI_ASSETS` overrides the folder). |

- **Threading:** RT64 calls the draw hook on its presentation thread, after drawing the game and before presenting. All RmlUi work happens there. Other threads use `request_open`, `request_close` and `queue_action`, which are applied at the next frame, and read the player's choices with `take_command`.
- **Scaling:** documents are designed for a 1080-pixel-high screen using `dp` units; the density-independent pixel ratio is the window height divided by 1080, so a layout looks the same at any resolution.
- **Resources:** released geometry and textures are kept for four frames before being freed, since RT64 may still be drawing earlier frames.
- **Fonts:** Lato (SIL Open Font License), family name `LatoLatin`. Others can be added to `assets/ui/fonts`.
- **Dependencies:** RmlUi 6.2 and FreeType 2.13.3 are pinned in `config/dependencies-m1.json` and built statically from `.local/deps` (`cmake/BlockPlanetUI.cmake`).

## Screens and input

A screen is an RML document in `ui/screens/<name>.rml` that links `../menu.rcss`. Elements with the class `item` take focus in document order (items with the class `disabled` are skipped); the focused one gets the class `focused`. Up and Down move the focus, wrapping around. Confirm runs the focused item's `data-action`:

| Action | Effect |
| --- | --- |
| `open:NAME` | Shows screen NAME on top. The screen being left keeps its focus for when the player comes back. |
| `back` | Goes back one screen; from the first screen it closes the menus. Also what the Back input does. |
| `close` | Closes the menus. |
| `command:NAME` | Reports NAME to the game, which logs it as `ui_command`. `exit` closes the game; `profiles` and the `profile…` commands run the profile screens; `story`, `time_trial`, `lines`, `puzzle`, `versus` (and `hide_and_seek`) start a mode (below); the others are not connected yet. |

**Setting rows** have the classes `item value` (add `slider` for a row that stops at its ends and shows a bar: a child `.bar` holding `.fill`), `data-setting="NAME"` and `data-options="value:Label,value:Label"`, and show the current value's label in a child with the class `value`. Left and Right (and Confirm) cycle the value and report `setting:NAME=VALUE`; the game applies and saves it through the same path as the PC settings menu, then sends the saved value back, so the row always shows what was saved. Current values come from `set_setting`.

**Grids:** items directly inside an element with `data-columns="N"` form a grid, filled row by row; an item with `data-span="K"` takes K columns. Left and Right move along a row, wrapping at its ends. Up and Down go to the item covering the same column in the next row, remembering the column across wide items; at the top and bottom they wrap if the grid has `data-wrap`, and otherwise leave the grid. Give grid items widths as shares of the row (see `.keys` in `menu.rcss`) so each row holds exactly N.

**Controller or keyboard:** while a controller is used, every screen's body has the class `pad`; `.for-pad` parts show only then and `.for-keys` parts only otherwise. The first controller press or keyboard key switches.

**Section headings:** an element with the class `section` labels the rows below it; it takes no focus.

**Text fields:** an element with `data-text="NAME"` shows the text given to `set_text` (as plain text).

**Lists the game fills in:** an element with `data-list="NAME"` gets the items given to `set_list` when its screen opens (label, note, action; disabled entries are skipped). An item marked `focus` takes the focus when the screen opens.

Screens get the class `entering` when opened and `returning` when shown again after going back, which `menu.rcss` uses for a quick slide (0.18 s).

Input while menus are open:

| | Keyboard | Controller |
| --- | --- | --- |
| Move | Arrow keys | D-pad, left stick |
| Confirm | Enter, Space | The profile's confirm button (A by default), Start |
| Back | Escape, Backspace | The profile's cancel button (B by default), Back/Select |

While menus are open the game receives no input, and after they close it stays blocked until every button is released, so the closing press does not reach the game. For now F1 opens and closes the menus, and `BLOCKPLANET_UI_TEST=1` opens them at start; the game keeps running behind them.

## Starting modes

`ModeLauncher` (`src/platform/mode_launcher.cpp`) starts the game's modes by driving the original menus, so the game sets each mode up exactly as when a player chooses it. From the title, the main menu or the player page it puts the original menu's cursor on the right item and presses A (on port 1, through `get_input`), step by step:

1. Main menu row (`0x800E121C`) to SINGLE, press, until the player page opens (`0x800E1222` ≥ 0).
2. Player page row to START, press, until its mode list is active (`0x800E1224` ≥ 0).
3. Mode list row to the mode's row, press, until play begins (`0x800E121C` = −1).

Mode list rows (the player page's table at `0x800EDAE4`): 0 Rescue (mode 1), 1 Hide & Seek (5), 2 Puzzle (4), 3 Time Trial (2), 4 VS CPU (6), 5 Lines (mode 9). The original menu lists only rows 0 to 4 until Lines is unlocked; the launcher starts Lines directly, which is how BlockPlanet makes it available from the start.

Requests are refused during play and inside other original pages such as Options, and give up after 15 seconds. The log shows `mode_launch` with `started`, `playing`, `refused` or `timed_out`. Checked from the title in the Linux test setup: Story (Rescue 1:1), Time Trial, Lines (its rules screen), Puzzle and Versus (VS CPU) all start.

## Screens connected to the game

| Screen | What it does |
| --- | --- |
| Options (`options.rml`) | **Graphics** and **Audio**, each its own screen. **Graphics** (`graphics.rml`): Sphere (Extended or Original), **Picture** (Fill window, the default: the picture takes the window's shape, ultrawide included; 16:9; or the original 4:3; applies at once), Display (window or full screen), Textures (three-point or linear) and Scaling (pixel, smooth or sharp), saved in the PC's settings. **Audio** (`audio_options.rml` from the menus; in a game, the pause version below): Music and Effects sliders and Music order (Auto or Random), saved in the profile (`preferences.audio` in `save.json`; a guest's last the session) and applied on the game's thread (`src/graphics/audio_hooks.cpp`) by scaling every volume the game sets, so they also cover song fades and jingles (which the original balance did not), and 50% sounds half as loud. Each change applies and saves at once. |
| Confirmation (`confirm.rml`) | A question set by the game (title, note and choices, the safe one selected): deleting a profile, restarting a Story chapter. |
| Profiles (`profiles.rml`, `profile.rml`, `confirm.rml`) | Main > Profiles, and at launch when there is more than one profile (unless `--profile` or `--guest` was given). Lists the playing profile first, the others, New profile ("Player N") and Play as guest. A profile offers Play as (or Keep playing), Rename, Character and Delete, which asks to confirm. New profile and Rename open the name screen (`name.rml`): typing on a keyboard enters text, Backspace deletes and Enter finishes; with a controller, letters are picked from an on-screen keyboard (QWERTY with the numbers on top, then Shift, Space, Delete and Done; it wraps in every direction; shift lasts one letter) and Start finishes. The on-screen keyboard is shown only while a controller is used. Names are 1 to 24 printable characters; a new profile left empty is called "Player N". The Character screen (`character.rml`) picks None or one of the six robots and a colour variant (Original, Crimson, Amber, Lime, Teal, Sky, Violet, Rose). Switching restarts the game (see [SAVES.md](SAVES.md#profiles)). Commands are listed in `include/tetrisphere/profile_menu.h`. |
| Pause (`pause.rml`) | Start (or F1) during play. The game's own pause still runs: it freezes play and draws its "paused" background, which hides the board so pausing gives no thinking time. BlockPlanet hides the rest of it (the white line, menu text, button hints and instructor, `src/graphics/pause_draw_hooks.cpp`) and shows its panel instead, headed PAUSED (the game's own "paused" lettering is hidden with the instructor, as they are drawn together): Resume, Restart level or Restart chapter (Story, Hide & Seek; after a confirmation) or Restart lesson (Training), Skip lesson (Training only), Options (Graphics, Audio) and Quit to menu, with where the player is as the subtitle (for example "Story · 1:1"). It opens only after a Start press (or F1): the game's chapter-end menu (Continue, Exit) uses the same row without one and is left alone. Choices drive the original pause (`PauseBridge`): its row (`0x800E1228`: 0 Continue, 1 Skip, 2 Audio, 3 Exit) is set and A pressed. Back (B, Esc) resumes. The original Audio page is not offered; audio settings will join Options. |
| Restart level (pause) | In modes with chapters whose game over sends the player back to the chapter's first level (Story and Hide & Seek, both tested) it is **Restart chapter**, after "Restart chapter? Progress within the chapter will be lost." (Yes / No, No selected): it starts the chapter from its first level, as the game's own Try Again after a game over does. Elsewhere it is **Restart level**: it quits through the game's own pause (Exit), then starts the same place again through the original menus: Puzzle at the same puzzle, Time Trial, Lines and VS CPU from their start, and the same Training lesson (Chapter 0). A fresh attempt: score and hearts start over; a track chosen in the pause menu is kept. Takes about 10 to 15 seconds through the game's menu transitions. Not offered in Training Advanced or modes BlockPlanet cannot start yet. |
| Pause → Options → Audio (`audio.rml`) | Music and Effects sliders, Music order (Auto, Random or **Choose**) and **Track**: Left and Right play the previous or next of the 23 tracks straight away (the game's CHOOSE). Choose and the track are an override for the current game: Quit to menu or starting another mode returns to the saved Auto or Random. |
| Stage lists (`stages.rml`) | Story > a chapter: Whole episode, then each stage completed and the next, with its best time; a stage starts that stage on its own (`stage:N:L`), which returns to the menu when completed. |
| Training (the menus' X button, 2026-11-07) | Training sits in the menu hierarchy, not in a list of its own (the owner's call): on an item that has a lesson, **X** (the pad's west button, or **T** on the keyboard) starts it, and the description ends with a hint, the button's symbol and "Training". Items with data-alt (`ui_navigator.h`): Solo's **Story**, and Story's **Campaign** and **Casual**, open **Training** (Basics: the shadow, sliding, dropping, combos and magic; Advanced: crystal pieces, the X count, gravity and fuse combos; the first not yet done is focused). Solo's **Puzzle** starts **Puzzle Training**. Lessons start through the original menus (main menu row 4, then BASICS, ADVANCED or PUZZLE) and return to the menus when done; finishing one is recorded in the profile. |
| Chapter lists (`chapters.rml`, titled by the game) | **Puzzle** opens the puzzle list directly. **Story** lists the completed chapters and the next one (up to 10, the game's episodes; 9 and 10 are bonus episodes), focused on the next. Other chapters start Rescue there (`GameOptionApplier::choose_chapter`, which, unlike `--start-chapter`, does not pause trophies or progress). A guest gets Chapters 0 and 1. |

## Status

- Placeholder screens follow the structure in `docs/REDESIGN.md` (2026-11-06): main menu (Continue when a game was interrupted, Solo, Local, Online, Profiles, Media Player, Trophies, Options, Credits, Exit), Solo (Story, Hide & Seek, Puzzle, Lines, Multiphasic later), Story (Campaign, Time Attack, Score Attack, Casual), Local (Story and Lines, each with the sub-modes Versus and Cooperative, and Custom; Versus then asks who player 2 is: the computer, or a second player later) and Online (placeholders). Items carry a `data-description` (or `ListItem::description`) shown in the screen's `#description` box while focused (`ui_navigator.cpp`); long lists use `body.compact`. Their layout is a placeholder until the art direction is set, but they use the original game's menu colours (sampled from its menus): items orange (the original `#E06018`, brightened to `#F5821F`; to refine with the art direction), the selected item white `#F8F8F8` on a faint bar, dark-outlined text, a white-outlined box, and yellow `#E8B818` for notes, values and hints.
- Checked in the Linux test setup (Vulkan, 720p): drawing, focus, opening Solo, a command, going back with focus remembered, and closing with F1.
- Checked in the Linux test setup: the Options screen shows and changes Sphere and Textures, and the settings file has the new values; with chapters 1 and 2 completed, Story lists Chapters 1 to 3 and Chapter 3 starts Rescue at 3:1.
- Checked in the Linux test setup: Picture set to Fill window fills 21:9, 32:9 and 16:10 windows on the title, in play (HUD at the edges) and in the pause; 4:3 is unchanged; switching to 16:9 during play pillarboxes the picture at once. Portrait windows show a small picture.
- Checked in the Linux test setup: Hide & Seek at level 3 offers Restart chapter (subtitle "Hide & Seek · Level 3") and goes back to level 1.
- Checked in the Linux test setup: in Story at 2:3, Restart chapter asks first with No selected; No returns to the pause menu and Yes starts 2:1. Restart level (as first built) at 2:3 after losing hearts started 2:3 again with full hearts and a new board; the game-over menu (Try Again, Exit) is left as the game shows it, without the pause screen over it; in Puzzle it restarts Puzzle 1 (later puzzles not yet checked).
- Checked in the Linux test setup (audio recorded from the game): the Music slider at 0 silences the title music and the game's music and effects levels follow the sliders; in a game, Pause → Audio → Track plays the next track at once and sets the game to Choose, and Quit to menu returns it to Auto. Options reviewed at 16:9 and 21:9.
- Checked in the Linux test setup: the pause screen opens with Start and with F1, hides the original pause's line, text and instructor, resumes with B, opens Options and returns, quits to the menu, and skips a Training lesson; reviewed at 16:9, 4:3 and 21:9.
- Checked in the Linux test setup: the launch prompt with two profiles, switching to the other profile (the game restarts as it), deleting another profile, adding "Player 2", deleting the playing profile (restarts as Player 1, folder moved to `deleted/`) and playing as a guest; renaming by typing, choosing a character and colour, and naming a new profile. The name and character screens were reviewed at 16:9, 4:3 and 21:9. The on-screen keyboard was checked with the QA virtual controller: it appears on the first controller press, Up from 5 reaches the Space bar and back, and a keyboard key hides it again.
- Not yet: mouse input, Custom and the other commands, showing the profile's character in the game, replacing the original title and mode menus, and checks on Windows. Taller-than-wide windows (portrait) still show a small picture: the renderer widens but does not heighten.

## Next

1. More options (audio, controls); showing the profile's character and unlocking characters as they are rescued (needs research).
2. Camera and screen sizes: portrait windows, and the sphere's framing at every shape (docs/REDESIGN.md).
3. Replacing the original title and mode-select menus with these screens.

## Menu grid

The owner's design (2026-11-09; roadmap: One grid background for the menus). BlockPlanet draws the game's tiled grid as one background of its own (`src/ui/ui_grid.cpp`, `assets/ui/grid.rml`), always behind the menu screens (its document is pushed to the back every frame) and above the game's picture and HUD.

- **Colour per screen:** `data-grid="blue"` (or green, red, purple, gold, grey; `grid_colour` in `ui_grid.cpp`) on a screen's body. The grid shows while the first open screen has one, in the colour of the highest open screen that has one. Screens over the grid get the class `on-grid` (their dimming background is dropped, `menu.rcss`).
- **Shades:** tile colours sampled from the original (blue `#0d151f`, highlights `#141f2d`, shadows `#080d14`; red `#2b0510`); green is a matching dark teal-green `#0b1d18`. The far background behind the tiles is the tile colour at 45 %.
- **Shape:** 9 × 7 tiles at 4:3, more columns on wider screens and more rows on taller ones (odd counts). Each tile is an `img` element with a 3D transform under one perspective from the screen's centre: tilted outwards by its place on a sphere, the middle ones a little nearer, shaded by a light from the upper left. Constants at the top of `ui_grid.cpp`.
- **Changes:** the same colour stays still. A new colour fades the far background and flips each tile half a turn about its upright axis, starting one after another from the top left (`grid_flip_seconds`, `grid_ripple_seconds`); past halfway a tile shows the new colour. A change part way through another starts each tile again from the colour it shows.
- **Texture:** the game's grey marble from the player's ROM (main archive entry `0x000`, 160 × 120, 8-bit; `grid_texture.cpp` unpacks pass J), read once at run time and kept only in memory. Four square parts of it, each with a bevel, make the tiles' texture, tinted by `image-color`. Without the ROM's texture a procedural marble is used.
- **Not yet (step 2):** the tiles flying out towards the camera into play and back in afterwards, with the loading cover, and red behind the game over panel.

**Testing without the game:** `cmake -S tests/ui -B build-ui && cmake --build build-ui && ctest --test-dir build-ui` runs the menu tests (RmlUi and FreeType in `.local/deps`). `cmake --build build-ui --target ui_capture`, then `build-ui/ui_capture assets/ui OUT [rom.z64]` writes captures of the main menu, Solo, Story, Options and the pause menu over the grid, and three frames of a colour change, at 4:3, 16:9, 21:9 and portrait, drawn by RmlUi in software (pictures such as controller art are left out). Captures stay out of Git.

## In-game HUD

BlockPlanet's HUD (design in [HUD_DESIGN.md](HUD_DESIGN.md)) is a separate RmlUi document, `assets/ui/hud.rml` with `hud.rcss`, in the same context as the menus. The game thread fills `blockplanet::ui::HudView` each frame (`update_hud` in `linux_main.cpp`, values from `src/platform/story_hud.cpp`) and calls `ui::set_hud`; the presentation thread rebuilds the HUD's markup only when it changes. The HUD is hidden while a menu is open and outside Story play. It takes no input. A media query (`orientation: portrait`) turns the two columns into rows above and below the game's picture.

Hiding the original HUD: hooks mark the game's single-player HUD routine (`func_80075CD8`), and while BlockPlanet's HUD is shown the draw filter skips the 2D helpers called inside it; the 3D piece queue (`func_800A3E6C`) is skipped too. Checked at 16:9, 4:3, 21:9 and portrait (600 × 840).

**Case (decided 2026-11-07):** no all-capitals text. BlockPlanet's own screens use Title Case for labels and sentence case for sentences, and the game's own capitals are converted the same way (`tetrisphere::proper_case`). The title's notice and the pause screen's heading follow it too. The one exception: a warning may use capitals for emphasis (sparingly). The name keyboard's SHIFT label shows capitals on.


## Key terms (highlights in speech and cards)

The game's key terms are highlighted in Training's speech and on every card, each on its first mention in a page, and no other words are (the owner's choice, 2026-11-08). The glossary is in `publish_game_text`, `linux_main.cpp`: shadow, slide and sliding, combo, combo counter, power combo, power brick, gravity combo, fuse and fuse combo, crystal brick, magic and Magic box, X count, wildcard, hearts, lives, core, drops and slides, drill. Game terms are light blue; where a card first mentions one it glows, and the page opens with a heading for it when it has none. A new term joins the glossary rather than being highlighted by hand.
