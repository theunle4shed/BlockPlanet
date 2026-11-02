# Menu layer (BlockPlanet)

BlockPlanet's new menus are built with [RmlUi](https://github.com/mikke89/RmlUi), an MIT-licensed library that lays out documents written in an HTML- and CSS-like language (RML and RCSS). BlockPlanet draws them over the game itself, through RT64.

RecompFrontend, the menu library other N64: Recompiled projects use, was not used: it has no licence, so BlockPlanet may not ship its code. Blocksphere kept it as a reference only. BlockPlanet's renderer was written independently.

## How it works

| Part | File | Role |
| --- | --- | --- |
| Renderer | `src/ui/ui_renderer.cpp` | Implements RmlUi's render interface on RT64's graphics layer (plume): geometry in host-visible buffers, textures uploaded through a staging buffer, scissor regions and transforms. Fonts and images become textures; PNG images are decoded with stb_image (compiled by RT64). |
| Shaders | `src/ui/shaders/UiVS.hlsl`, `UiPS.hlsl` | Pixels to clip space with RmlUi's transform; premultiplied-alpha colour times the texture. Compiled with RT64's DXC to SPIR-V (and DXIL on Windows). |
| System | `src/ui/ui_system.cpp` | Registers RT64's render hooks before RT64 starts, starts RmlUi at the first frame, loads fonts and documents, and updates and draws the context every frame. |
| Assets | `assets/ui/` | Documents and fonts, copied beside the game into `ui/` at build time (`BLOCKPLANET_UI_ASSETS` overrides the folder). |

- **Threading:** RT64 calls the draw hook on its presentation thread, after drawing the game and before presenting. All RmlUi work happens there. Input from the main thread will be queued to it.
- **Scaling:** documents are designed for a 1080-pixel-high screen using `dp` units; the density-independent pixel ratio is the window height divided by 1080, so a layout looks the same at any resolution.
- **Resources:** released geometry and textures are kept for four frames before being freed, since RT64 may still be drawing earlier frames.
- **Fonts:** Lato (SIL Open Font License), family name `LatoLatin`. Others can be added to `assets/ui/fonts`.
- **Dependencies:** RmlUi 6.2 and FreeType 2.13.3 are pinned in `config/dependencies-m1.json` and built statically from `.local/deps` (`cmake/BlockPlanetUI.cmake`).

## Status

- `BLOCKPLANET_UI_TEST=1` shows a test screen (`assets/ui/test.rml`): a panel with a title, text and a highlighted item. It draws correctly over the game at 720p (checked in the Linux test setup, Vulkan).
- Not yet: input (controller, keyboard and mouse navigation), real screens, transitions between screens, pausing the game behind a menu, and checks on Windows.

## Next

1. Input: controller and keyboard navigation through the existing input layer, queued to the presentation thread.
2. A screen framework: showing and hiding screens with quick transitions, and a focus model suited to controllers.
3. The first real screens: main menu, then chapter select and the profile screen, following the UI rules in `AGENTS.md` (modern, simple, TV-like, controller-first).
