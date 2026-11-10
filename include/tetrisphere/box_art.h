#pragma once

// The box art on the grid (docs/UI.md, "The box art on the grid"): while the
// game's title (BlockPlanet's held title scene, "the box art") shows, its
// grid sphere backdrop is not drawn; a fill takes its place, and BlockPlanet's
// menu grid is drawn under the game's picture instead
// (tetrisphere/under_layer.h). BLOCKPLANET_BOX_ART=0 keeps the game's backdrop.

#include <atomic>
#include <cstdlib>

namespace tetrisphere::box_art {

// BLOCKPLANET_BOX_ART=0 turns the box art's grid and flow off.
inline bool enabled() {
    static const bool on = [] {
        const char* v = std::getenv("BLOCKPLANET_BOX_ART");
        return v == nullptr || v[0] != '0';
    }();
    return on;
}

// Set by the game's thread while the box art shows over BlockPlanet's grid.
inline std::atomic<bool>& showing() {
    static std::atomic<bool> on{false};
    return on;
}

// Where the box art's attract cycle is (game thread writes, main loop reads):
// ToDemo from the attract timer until the demo plays (the grid stays up over
// the game's menu and BlockPlanet's menus stay closed), Demo while it plays
// (the grid flies out), None otherwise.
enum class Phase { None, ToDemo, Demo };
inline std::atomic<Phase>& phase() {
    static std::atomic<Phase> value{Phase::None};
    return value;
}

// Set when the intro's scene reaches the hand-over frame (box_art_hooks.cpp,
// inside the intro's own frame); the hand-over itself is done at the start of
// the game's next frame, before the intro runs (direct_start_hooks.cpp).
inline std::atomic<bool>& handover_pending() {
    static std::atomic<bool> pending{false};
    return pending;
}

} // namespace tetrisphere::box_art
