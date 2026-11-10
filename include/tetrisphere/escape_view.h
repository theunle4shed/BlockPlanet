#pragma once

// Escape mode, step 1: a visual prototype of the view from inside the play
// sphere (docs/ROADMAP.md, New game modes, Escape; research,
// BLOCKPLANET_ESCAPE_VIEW=1, off by default). The rules do not change.
//
// In place of the game's sphere drawing, BlockPlanet draws the board as seen
// from the sphere's centre, looking out at its current face: for each cell of
// the game's visible patch (the 15 x 15 cells between the mesh table's 16 x 16
// points, 0x800E44E4), the underside of its first block above the core (layer
// 1), coloured by brick type, with a dark bevel. The core is not drawn; a
// column dug down to it is a hole. Its own camera (at the centre) and
// projection; the game's cursor, pieces and magic are drawn as before, from
// outside (what that breaks is the point of the prototype).

#include <array>
#include <cstdint>
#include <vector>

namespace tetrisphere::escape_view {

bool enabled();
// Called at the start of the sphere's drawing (the first quadrant pass):
// adds the inside view to the game's display list. Returns whether it drew
// (then the game's sphere is skipped).
bool draw(std::uint8_t* rdram);

// For testing: the cells the view draws, as quads (4 corners) with a colour.
struct Quad { std::array<std::array<std::int16_t, 3>, 4> corner; std::array<std::uint8_t, 3> colour; bool hole; };
std::vector<Quad> cells(const std::uint8_t* rdram);
// Writes the view's display list into BlockPlanet's memory for it and
// returns its address (0 if it does not fit). Ends with G_ENDDL.
std::uint32_t build(std::uint8_t* rdram, std::uint32_t projection, float fov_degrees);

constexpr std::uint32_t memory = 0x805F0000u;      // the tail of the extended sphere's first vertex buffer
constexpr std::uint32_t memory_end = 0x80600000u;

} // namespace tetrisphere::escape_view
