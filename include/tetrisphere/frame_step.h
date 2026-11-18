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
// From the renderer, before it draws each display list: waits while frozen,
// calling `present` about 60 times a second meanwhile (the screen updated).
void gate(const std::function<void()>& present);
// Whether the game is held now, waiting for a step (any thread).
bool waiting();
// The renderer's hand on RT64's own pause (set once it exists).
void set_picture_pauser(std::function<void(bool)> pause);

// The free camera on a joypad (the owner, 2026-11-18: "the analog sticks
// control the camera, whereas the D-pad controls the cursor"). While RT64's
// free camera is on (Free Camera Enabled, in its inspector's Debugger tab),
// player 1's left stick moves it, the right stick turns it and the triggers
// lower and raise it (it is turned on, and moved from 3D scene to 3D scene,
// with F8 or the right stick's click, and off with F9 or the left stick's); the game then takes its directions from the D-pad only
// (and not the triggers).
struct CameraMove {
    float right = 0, up = 0, forward = 0;  // -1 to 1 each, times the seconds since the last
    float yaw = 0, pitch = 0;              // radians
    // 1: the free camera on, or on the frame's next 3D scene (RT64's own
    // checkbox always takes the first, often not the one wanted); -1: off.
    int scene = 0;
};
// F8 / the right stick's click: on, or the next scene; F9 / the left stick's click: off.
void request_scene(int change);
// The renderer's hand on RT64's camera: moves it and returns whether the
// free camera is on (nothing moved when it is off).
void set_camera_driver(std::function<bool(const CameraMove&)> drive);
// From the main loop each frame: false while there is no free camera.
bool drive_camera(const CameraMove& move);
// Whether the free camera has the sticks now (any thread).
bool camera_has_sticks();

} // namespace tetrisphere::frame_step
