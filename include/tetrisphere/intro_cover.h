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
#include <cstdlib>

namespace tetrisphere {

// Set by the game's thread while the intro plays; read by RT64's render thread.
inline std::atomic<bool>& intro_cover_active() {
    static std::atomic<bool> active{false};
    return active;
}

// Very wide windows during the intro (the owner's choice, 2026-11-12): the
// intro's scenes were made for a 4:3 picture, and wider than 16:9 their
// edges show cut bricks and long streaks of bricks passing the camera. The
// intro is shown no wider than 16:9, between black bars; the title and
// everything after it fill the window. BLOCKPLANET_INTRO_MAX_ASPECT=ratio
// changes the limit (0 for none).
inline float intro_max_aspect() {
    static const float value = [] {
        const char* v = std::getenv("BLOCKPLANET_INTRO_MAX_ASPECT");
        return v != nullptr ? static_cast<float>(std::atof(v)) : 16.0f / 9.0f;
    }();
    return value;
}

// RT64's Expand target: the window's shape, but never narrower than the
// game's own, except during the intro (zoom to cover), and during the intro
// no wider than `intro_max` (none when 0 or less).
constexpr float expand_aspect_target(float window, float source, bool intro, float intro_max = 0.0f) {
    if (!intro) return std::max(window, source);
    return intro_max > 0.0f ? std::min(window, std::max(intro_max, source)) : window;
}

} // namespace tetrisphere
