#pragma once

// Escape mode, step 1: a visual prototype of the view from inside the play
// sphere (docs/ROADMAP.md, New game modes, Escape; research,
// BLOCKPLANET_ESCAPE_VIEW=1, off by default). The rules do not change.
//
// In place of the sphere's usual drawing, the game's own board is drawn from
// the sphere's centre, looking out at its current face: for each cell of the
// visible patch, the inner (core-facing) face of its first block above the
// core, drawn by the game's own inner-face routine (func_8009C6D4, which the
// game never calls) with the block's own texture coordinates and shading, so
// the bricks are the board's real ones (src/graphics/sphere_quadrants.cpp,
// draw_inside). The core is not drawn. This file is the camera: its own
// projection and a view from the centre, pushed before and popped after.

#include <cstdint>

namespace tetrisphere::escape_view {

bool enabled();
// Appends the inside camera (its projection, and its view pushed) to the
// game's display list. Returns false (nothing added) if it cannot.
bool begin(std::uint8_t* rdram);
// Pops the view and loads the game's projection again.
void end(std::uint8_t* rdram);

// The camera's matrices, written to `memory`, for a field of view: from the
// centre of the mesh table's sphere (0x800E44E4) towards its patch's middle.
// Returns false if the mesh table is not there.
bool write_camera(std::uint8_t* rdram, float fov_degrees);

constexpr std::uint32_t memory = 0x805F0000u;  // two matrices (the tail of the extended sphere's first vertex buffer)

} // namespace tetrisphere::escape_view
