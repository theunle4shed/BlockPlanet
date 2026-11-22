#pragma once

// The Missile at level 3, the barrage (the owner, 2026-11-22): after the
// game's flight brings the loaded launcher in, BlockPlanet draws it hovering
// over the cursor, then its three missiles dropping onto the three strike
// points (5 cells out at 0, 120 and 240 degrees) as the blasts go off, then
// the empty launcher leaving. Drawn on the sphere (its own coordinates), with
// the extended sphere's mesh, after the game's bricks.

#include <cstdint>

namespace tetrisphere::sphere {
class ExtendedSphere;
}

namespace tetrisphere::barrage {

// The strikes' centres, relative to the cursor's cell, in board cells.
void strike_offset(int n, double& dx, double& dy);

// From the game (magic_hooks.cpp): the launcher's flight has ended
// (`cursor_x`, `cursor_y` the cursor's board cell); the blasts went off; each
// game frame.
void begin(int cursor_x, int cursor_y);
void impact();
void frame();
bool active();

// From the sphere's drawing, after its bricks (sphere_hooks.cpp).
void draw(std::uint8_t* rdram, const sphere::ExtendedSphere& sphere);

// Crosshairs (the owner, 2026-11-22: the game's target is a bitmap that
// pixelates, and one target for several strikes): rings drawn on the sphere's
// surface, each the size of its blast, from the power-up's use until its
// blast. Centres in board cells (fractional: a blast's a0 / 256 + 0.5).
struct Target {
    double x, y, radius;
    std::uint32_t rgb;
    bool big = false;  // a bigger blast: a yellow outer ring and arrows pointing out
};
void set_targets(const Target* targets, int count);
// While a power-up is held: where it would land, following the cursor (set
// each frame; count 0 clears).
void set_aim(const Target* targets, int count);
// A blast went off at (a0, a1) (1/256 of a cell): the crosshair there goes.
void blast_at(int a0, int a1);
void clear_targets();
// While crosshairs show, the game's own target (drawn with the cursor,
// func_8009AB60) is not drawn.
bool hide_game_target();

} // namespace tetrisphere::barrage
