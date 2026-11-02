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
| `command:NAME` | Reports NAME to the game, which logs it as `ui_command`. `exit` closes the game; the other commands will start modes and open the new options screens. |

Screens get the class `entering` when opened and `returning` when shown again after going back, which `menu.rcss` uses for a quick slide (0.18 s).

Input while menus are open:

| | Keyboard | Controller |
| --- | --- | --- |
| Move | Arrow keys | D-pad, left stick |
| Confirm | Enter, Space | The profile's confirm button (A by default), Start |
| Back | Escape, Backspace | The profile's cancel button (B by default), Back/Select |

While menus are open the game receives no input, and after they close it stays blocked until every button is released, so the closing press does not reach the game. For now F1 opens and closes the menus, and `BLOCKPLANET_UI_TEST=1` opens them at start; the game keeps running behind them.

## Status

- Placeholder screens follow the planned structure (`docs/REDESIGN.md`): main menu (Solo, Multiplayer, Media Player, Trophies, Options, Credits, Exit), Solo (Story, Time Trial, Lines, Puzzle, Custom, Training) and Multiplayer (Versus, Collaborate, Custom). Their look is a plain placeholder until the art direction is set.
- Checked in the Linux test setup (Vulkan, 720p): drawing, focus, opening Solo, a command, going back with focus remembered, and closing with F1.
- Not yet: Left and Right on value items (settings), mouse input, starting modes from commands, replacing the original title and mode menus, pausing the game behind a menu, and checks on Windows.

## Next

1. Starting the game's modes from menu commands (Story, Time Trial, Puzzle, Lines, Versus), through the mode numbers already mapped.
2. Value items for settings, then the options, chapter select and profile screens.
3. Replacing the original title and mode-select menus with these screens.
