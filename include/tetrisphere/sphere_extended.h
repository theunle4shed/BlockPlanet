#pragma once

// BlockPlanet's extended sphere patch (sphere renderer, stage 2).
//
// The game draws 15 x 15 cells from a 16 x 16 point mesh. The extended patch
// adds `ring` cells on every side. Its mesh and lighting normals are kept on
// the host, built each frame alongside the game's own (which the game's
// other passes still use), with the same formulas. See
// docs/research/blockplanet-game-state.md.

#include "tetrisphere/sphere_cell.h"
#include "tetrisphere/sphere_grid.h"
#include "tetrisphere/sphere_mesh.h"

#include <array>
#include <cstdint>
#include <vector>

namespace tetrisphere::sphere {

using Normal = std::array<std::int16_t, 3>;  // vertex component order, scale 120

class ExtendedSphere {
public:
    // Cells run from -ring to 14 + ring in both directions (game numbering).
    int ring() const { return ring_; }
    int first_cell() const { return -ring_; }
    int last_cell() const { return 14 + ring_; }

    // Builds the mesh and normals. `inputs` are the game's mesh inputs for
    // this frame (rows, columns, base and wide are set here).
    void build(int ring, const MeshInputs& inputs, const ExtendedBaseGrid& grid);

    // Mesh points run from -ring to 16 + ring (one beyond the last cell's far
    // corner, for the far edge normals).
    const MeshEntry& point(int row, int column) const { return mesh_[index(row, column)]; }
    const Normal& point_normal(int row, int column) const { return point_normals_[index(row, column)]; }
    // Side normals: the edge between mesh rows `row` and `row + 1` (taken
    // along the middle column), and between columns likewise.
    const Normal& row_normal(int row) const { return row_normals_[static_cast<std::size_t>(row + ring_)]; }
    const Normal& column_normal(int column) const { return column_normals_[static_cast<std::size_t>(column + ring_)]; }

private:
    std::size_t index(int row, int column) const {
        return static_cast<std::size_t>((row + ring_) * points_ + (column + ring_));
    }

    int ring_ = 0;
    int points_ = 0;
    std::vector<MeshEntry> mesh_;
    std::vector<Normal> point_normals_;
    std::vector<Normal> row_normals_;
    std::vector<Normal> column_normals_;
};

// The game's normal: `scale` x vector / length, rounded toward zero, in
// single precision as the game computes it. Components are given in vertex
// order.
Normal game_normal(std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t scale);

// Writes the same work-area values as the game's cell setup (prepare_cell)
// for the cell at (row, column), taking mesh points and normals from
// `sphere` instead of the game's 16 x 16 tables.
void prepare_extended_cell(GuestMemory& memory, const ExtendedSphere& sphere, int row, int column);

} // namespace tetrisphere::sphere
