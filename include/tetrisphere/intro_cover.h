#pragma once

// Portrait windows during the intro (docs/research/blockplanet-game-state.md,
// "The intro on other screen shapes"). RT64's Expand mode never makes the
// frame narrower than the game's 4:3, so a tall window is letterboxed. While
// the intro plays (from the N64 logo to the title), BlockPlanet lets the frame
// take the window's own shape instead: RT64 then narrows the view, so the
// picture fills the window's height and its sides are cropped (zoom to cover),
// as the owner chose (2026-11-10). Everything else keeps the letterbox.

#include <algorithm>
#include <atomic>

namespace tetrisphere {

// Set by the game's thread while the intro plays; read by RT64's render thread.
inline std::atomic<bool>& intro_cover_active() {
    static std::atomic<bool> active{false};
    return active;
}

// RT64's Expand target: the window's shape, but never narrower than the
// game's own, except during the intro (zoom to cover).
constexpr float expand_aspect_target(float window, float source, bool intro) {
    return intro ? window : std::max(window, source);
}

} // namespace tetrisphere
