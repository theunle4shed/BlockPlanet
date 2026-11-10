#pragma once

// The box art on the grid (docs/UI.md, "The box art on the grid"): while the
// game's title (BlockPlanet's held title scene, "the box art") shows, its
// grid sphere backdrop is not drawn; a fill takes its place, and BlockPlanet's
// menu grid is drawn under the game's picture instead
// (tetrisphere/under_layer.h). BLOCKPLANET_BOX_ART=0 keeps the game's backdrop.

#include <atomic>

namespace tetrisphere::box_art {

// Set by the game's thread while the box art shows over BlockPlanet's grid.
inline std::atomic<bool>& showing() {
    static std::atomic<bool> on{false};
    return on;
}

} // namespace tetrisphere::box_art
