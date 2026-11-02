#include "tetrisphere/sphere_mesh.h"

#include <cassert>
#include <vector>

using namespace tetrisphere::sphere;

int main() {
    MeshInputs in;
    in.base = [](int row, int column) {
        return std::array<std::int32_t, 3>{row * 1000 + column * 32, -column * 64, -20000};
    };
    std::vector<MeshEntry> mesh;
    build_mesh(in, mesh);
    assert(mesh.size() == 256);
    // No rotation: entry (row, column) comes from base (row, column + 1), >> 5.
    for (int row : {0, 7, 15}) {
        for (int column : {0, 9, 15}) {
            const auto b = in.base(row, column + 1);
            const MeshEntry& e = mesh[static_cast<std::size_t>(row * 16 + column)];
            for (int axis = 0; axis < 3; ++axis) {
                const int v = b[axis] >> 5;
                assert(e.outer[axis] == v / 3);
                assert(e.inner[axis] == v / 10);
            }
        }
    }
    // A full row step moves halfway's worth of the way to the next row.
    in.row_fraction = 0x80;  // 0x80 << 8 = 0x8000, half of 0x10000
    build_mesh(in, mesh);
    assert(mesh[0].outer[0] == ((32 + 500) >> 5) / 3);
    // Scale halves every axis; two-player offsets x by +0x40 for player one.
    in.row_fraction = 0;
    in.scale_enabled = true;
    in.scale = {0x2000, 0x2000, 0x2000};
    in.two_player = true;
    build_mesh(in, mesh);
    const int x = ((32 >> 5) * 0x2000) >> 14;
    assert(mesh[0].outer[0] == x / 3 + 0x40);
    // Larger grids are supported for later stages.
    in.rows = 20;
    in.columns = 20;
    build_mesh(in, mesh);
    assert(mesh.size() == 400);

    // Wide values: points beyond 16 bits stay correct.
    MeshInputs wide;
    wide.base = [](int, int) { return std::array<std::int32_t, 3>{40000, -40000, 1000}; };
    wide.wide = true;
    build_mesh(wide, mesh);
    assert(mesh[0].outer[0] == (40000 >> 5) / 3);
    assert(mesh[0].outer[1] == (-40000 >> 5) / 3);
    return 0;
}
