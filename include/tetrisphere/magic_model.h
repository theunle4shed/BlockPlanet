#pragma once

// The held magic's 3D model in BlockPlanet's item box (src/graphics/magic_hooks.cpp).

namespace blockplanet {

// Where the item box is, in the game's picture: x and y in picture heights
// from its centre (x right, y down), and the model's size there (1 = the
// game's size above the bowl). `x_anchored` is the box's x in the original
// 4:3 frame, measured from its left edge as the box is from the picture's
// (the held draw is a left-anchored UI span); `anchor_shift` is how far such
// a span moves things on this picture (half its width beyond 4:3). With shown false, or x or y NaN
// (the box is outside the picture), the game's own placement is kept.
void set_magic_box(bool shown, float x, float y, float scale, float x_anchored, float anchor_shift);

// True while the held magic's model is being drawn in the item box.
bool magic_box_active();

} // namespace blockplanet
