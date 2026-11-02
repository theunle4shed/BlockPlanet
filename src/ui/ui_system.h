#pragma once

// BlockPlanet's menu layer: RmlUi drawn over the game through RT64's draw
// hook (docs/UI.md).

#include <filesystem>

namespace blockplanet::ui {

// Registers the menu layer with RT64. Call before RT64 starts. `assets` is
// the folder holding the menu documents and fonts (ui/ beside the game).
// Nothing is drawn unless a document is shown; BLOCKPLANET_UI_TEST=1 shows
// the test screen.
void install(const std::filesystem::path& assets);

} // namespace blockplanet::ui
