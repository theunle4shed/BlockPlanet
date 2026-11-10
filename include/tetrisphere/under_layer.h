#pragma once

// A layer drawn under the game's picture (docs/UI.md, "The box art on the
// grid"). RT64 normally draws the game's picture opaque, then BlockPlanet's
// menus over it. While the under layer is wanted, RT64's patched present
// (tools/build/patch_rt64_probe.py) first calls `draw` (BlockPlanet draws the
// menu grid there), then lays the game's picture over it by the picture's
// coverage: where the game drew (alpha 0 in its frame) it shows, where only
// BlockPlanet's backdrop fill was (alpha 1) the grid shows through.

#include <atomic>

namespace plume {
struct RenderCommandList;
struct RenderFramebuffer;
}

namespace tetrisphere::under_layer {

// Draws the under layer; returns whether it drew (the picture is then blended).
using DrawFunction = bool (*)(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer);

inline std::atomic<DrawFunction>& draw_function() {
    static std::atomic<DrawFunction> function{nullptr};
    return function;
}

// Set by the game's thread while the game's picture is to be laid over the layer.
inline std::atomic<bool>& wanted() {
    static std::atomic<bool> on{false};
    return on;
}

} // namespace tetrisphere::under_layer
