#include "tetrisphere/sphere_extended.h"

#include <cmath>

namespace tetrisphere::sphere {

Normal game_normal(std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t scale) {
    const std::int32_t length_squared = a * a + b * b + c * c;
    const float length = length_squared > 0 ? std::sqrt(static_cast<float>(length_squared)) : 1.0f;
    auto component = [&](std::int32_t v) {
        return static_cast<std::int16_t>(static_cast<std::int32_t>(static_cast<float>(v * scale) / length));
    };
    return {component(a), component(b), component(c)};
}

void ExtendedSphere::build(int ring, const MeshInputs& inputs, const ExtendedBaseGrid& grid) {
    ring_ = ring;
    points_ = 17 + 2 * ring;
    // The mesh builder reads base(row, column) and its neighbours from 0;
    // shift by the ring so 0 is the first extra point.
    MeshInputs in = inputs;
    in.rows = points_;
    in.columns = points_;
    in.wide = true;
    in.base = [&grid, ring](int row, int column) { return grid.point(row - ring, column - ring); };
    build_mesh(in, mesh_);

    // Point normals: -120 x the layer step, in vertex order (mesh component
    // 1, 0, 2), as func_800A2374 does.
    point_normals_.resize(mesh_.size());
    for (std::size_t i = 0; i < mesh_.size(); ++i) {
        const auto& step = mesh_[i].inner;
        point_normals_[i] = game_normal(step[1], step[0], step[2], -120);
    }
    // Side normals: +120 x the difference of neighbouring outer points along
    // the middle mesh row or column (7 in game numbering). The game keeps
    // the differences in 16 bits.
    constexpr int middle = 7;
    auto difference = [](const MeshEntry& from, const MeshEntry& to) {
        std::array<std::int32_t, 3> d{};
        for (int axis = 0; axis < 3; ++axis) d[axis] = static_cast<std::int16_t>(to.outer[axis] - from.outer[axis]);
        return game_normal(d[1], d[0], d[2], 120);
    };
    row_normals_.resize(static_cast<std::size_t>(points_ - 1));
    column_normals_.resize(static_cast<std::size_t>(points_ - 1));
    for (int k = -ring; k < points_ - 1 - ring; ++k) {
        row_normals_[static_cast<std::size_t>(k + ring)] = difference(point(k, middle), point(k + 1, middle));
        column_normals_[static_cast<std::size_t>(k + ring)] = difference(point(middle, k), point(middle, k + 1));
    }
}

} // namespace tetrisphere::sphere
