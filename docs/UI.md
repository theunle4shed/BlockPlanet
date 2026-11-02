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

**Setting rows** have the classes `item value`, `data-setting="NAME"` and `data-options="value:Label,value:Label"`, and show the current value's label in a child with the class `value`. Left and Right (and Confirm) cycle the value and report `setting:NAME=VALUE`; the game applies and saves it through the same path as the PC settings menu, then sends the saved value back, so the row always shows what was saved. Current values come from `set_setting`.

**Lists the game fills in:** an element with `data-list="NAME"` gets the items given to `set_list` when its screen opens (label, note, action; disabled entries are skipped).

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
| Options (`options.rml`) | Sphere (Extended or Original), Display (window or full screen), Textures (three-point or linear) and Scaling (pixel, smooth or sharp). Each change applies and saves at once. |
| Profiles (`profiles.rml`, `profile.rml`, `confirm.rml`) | Main > Profiles, and at launch when there is more than one profile (unless `--profile` or `--guest` was given). Lists the playing profile first, the others, New profile ("Player N") and Play as guest. A profile offers Play as (or Keep playing) and Delete, which asks to confirm. Switching restarts the game (see [SAVES.md](SAVES.md#profiles)). Commands are listed in `include/tetrisphere/profile_menu.h`. |
| Story chapter select (`chapters.rml`) | Story opens it when the profile has completed a Rescue chapter: the completed chapters and the next one (up to 15). Choosing one starts Rescue there (`GameOptionApplier::choose_chapter`, which, unlike `--start-chapter`, does not pause trophies or progress). Without a completed chapter, or as a guest, Story starts Rescue directly. |

## Status

- Placeholder screens follow the planned structure (`docs/REDESIGN.md`): main menu (Solo, Multiplayer, Media Player, Trophies, Options, Credits, Exit), Solo (Story, Time Trial, Lines, Puzzle, Custom, Training) and Multiplayer (Versus, Collaborate, Custom). Their look is a plain placeholder until the art direction is set.
- Checked in the Linux test setup (Vulkan, 720p): drawing, focus, opening Solo, a command, going back with focus remembered, and closing with F1.
- Checked in the Linux test setup: the Options screen shows and changes Sphere and Textures, and the settings file has the new values; with chapters 1 and 2 completed, Story lists Chapters 1 to 3 and Chapter 3 starts Rescue at 3:1.
- Checked in the Linux test setup: the launch prompt with two profiles, switching to the other profile (the game restarts as it), deleting another profile, adding "Player 2", deleting the playing profile (restarts as Player 1, folder moved to `deleted/`) and playing as a guest.
- Not yet: mouse input, Training, Custom and the other commands, naming profiles and choosing a character, replacing the original title and mode menus, pausing the game behind a menu, and checks on Windows.

## Next

1. Naming profiles and choosing a character (needs text entry and the character list), then more options (audio, controls).
2. Training and a pause screen.
3. Replacing the original title and mode-select menus with these screens.
