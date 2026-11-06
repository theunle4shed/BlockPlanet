#pragma once

// The held magic's 3D model in BlockPlanet's item box (src/graphics/magic_hooks.cpp).

namespace blockplanet {

// Where the item box is, in the game's picture: x and y in picture heights
// from its centre (x right, y down), and the model's size there (1 = the
// game's size above the bowl). `x_anchored` is the box's x in the original
// 4:3 frame, measured from its left edge as the box is from the picture's
// (the held draw is a left-anchored UI span); `anchor_shift` is how far such
// a span moves things on this picture (half its width beyond 4:3).
// `box_width` and `box_height` (picture heights) size the tint the game draws
// behind the model, so it is truly behind it; 2D drawing in the span moves by
// `anchor_shift_2d` (the full half-width beyond 4:3; 3D moves less). With shown false, or x or y NaN
// (the box is outside the picture), the game's own placement is kept.
void set_magic_box(bool shown, float x, float y, float scale, float x_anchored, float anchor_shift,
                   float box_width = 0.0f, float box_height = 0.0f, float anchor_shift_2d = 0.0f);

// True while the held magic's model is being drawn in the item box.
bool magic_box_active();

} // namespace blockplanet
