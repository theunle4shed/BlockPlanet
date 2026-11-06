#pragma once

// BlockPlanet's pause menu over the game's pause (src/graphics/pause_draw_hooks.cpp).

namespace blockplanet {

// While true and the game is paused, the game draws only its pause
// background (no line, menu text, hints or instructor).
void set_pause_menu_shown(bool shown);

// While true, the game's single-player HUD routine (func_80075CD8) skips its
// 2D draws and the 3D piece queue is not drawn, so BlockPlanet's own HUD can
// replace them. The routine's logic (magic launch, jingles) still runs.
void set_original_hud_hidden(bool hidden);

} // namespace blockplanet
