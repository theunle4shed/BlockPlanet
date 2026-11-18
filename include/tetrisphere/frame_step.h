#pragma once

// Developer tools: the game frozen and stepped a frame at a time (the
// owner's request, 2026-11-18: "pause it almost straight away as soon as it
// loads and check out the intro sequence frame by frame"). RT64's own Pause
// (F4 in its inspector) only freezes the picture: the game runs on behind it.
// This holds each display list the game hands to the renderer (once a frame),
// so the game waits for it, and lets one through at a time, pausing RT64's
// picture on the last so its inspector and free camera can look at it. Only
// with --developer.
//   F6  freeze or carry on
//   F7  one frame on (held: frame after frame)
// --developer-frozen starts frozen, before the first frame is drawn, with
// the inspector open.

#include <functional>

namespace tetrisphere::frame_step {

void set_enabled(bool developer);
bool enabled();
void set_frozen(bool frozen);
bool frozen();
void step();  // one frame on (freezes first if not frozen)
// From the renderer, before it draws each display list: waits while frozen.
void gate();
// Whether the game is held now, waiting for a step (any thread).
bool waiting();
// The renderer's hand on RT64's own pause (set once it exists).
void set_picture_pauser(std::function<void(bool)> pause);

} // namespace tetrisphere::frame_step
