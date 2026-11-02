#pragma once

// BlockPlanet's reimplementation of the six sphere face functions
// (originally func_8009C330, func_8009C6D4, func_8009CA98, func_8009CE38,
// func_8009D1D4, func_8009D570), stage 1 of the sphere renderer.
//
// Each draws one face of the current block as a quad: it appends two
// triangle commands to the display list and four vertices to the vertex
// buffer, flushing a "load 16 vertices" command first when the vertex batch
// is full. See docs/research/blockplanet-game-state.md.

#include "tetrisphere/sphere_cell.h"

namespace tetrisphere::sphere {

// Named by the face-mask bit that selects them (see sphere_quadrants.h).
enum class Face {
    Outer,       // 0x10, func_8009C330: away from the core
    Inner,       // func_8009C6D4: towards the core (not drawn by the quadrant passes)
    RowStart,    // 0x04, func_8009CA98: the side on the cell's first row edge
    RowEnd,      // 0x08, func_8009CE38: the side on the next row edge
    ColumnStart, // 0x01, func_8009D1D4: the side on the cell's first column edge
    ColumnEnd,   // 0x02, func_8009D570: the side on the next column edge
};

// Does exactly what the original face function does to guest memory.
void draw_face(GuestMemory& memory, Face face);

} // namespace tetrisphere::sphere
