#pragma once

// BlockPlanet's reimplementation of the four sphere quadrant passes
// (originally func_8009E630, func_8009EB28, func_8009F024, func_8009F520),
// stage 1 of the sphere renderer.
//
// The game draws a 15 x 15 cell patch of the sphere as four quarters. Each
// quarter walks its cells from the patch edge towards the centre (far to
// near), and for every block in a cell's column draws the faces the block
// mask marks as exposed: the outer face, plus the two side faces that look
// towards the centre of the view from that quarter. The face drawing itself
// is still the game's own code (func_8009C330 and its siblings).

#include "recomp.h"

#include <cstdint>

namespace tetrisphere::sphere {

enum class Quadrant { RowsUpColumnsUp, RowsDownColumnsUp, RowsUpColumnsDown, RowsDownColumnsDown };

// Does what the original pass for `quadrant` does, calling the same game
// functions in the same order.
void draw_quadrant(std::uint8_t* rdram, recomp_context* ctx, Quadrant quadrant);

} // namespace tetrisphere::sphere
