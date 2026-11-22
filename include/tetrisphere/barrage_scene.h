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

} // namespace tetrisphere::barrage
