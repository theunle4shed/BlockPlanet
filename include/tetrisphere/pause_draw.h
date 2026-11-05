#pragma once

// BlockPlanet's pause menu over the game's pause (src/graphics/pause_draw_hooks.cpp).

namespace blockplanet {

// While true and the game is paused, the game draws only its pause
// background (no line, menu text, hints or instructor).
void set_pause_menu_shown(bool shown);

} // namespace blockplanet
