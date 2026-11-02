#include "tetrisphere/sphere_grid.h"

#include <cmath>
#include <utility>

namespace tetrisphere::sphere {

std::array<double, 3> grid_formula_point(double row, double column, const GridGeometry& g) {
    constexpr double degrees = 3.14159265358979323846 / 180.0;
    const double tilt = (g.centre_row - row) * g.step_degrees * degrees;
    const double turn = (g.centre_column - column) * g.step_degrees * degrees;
    // Direction of the ray from S: start towards the centre (+z), turn by
    // `turn` towards +y, then tilt the plane by `tilt` about the y direction.
    const double dx = std::cos(turn) * std::sin(tilt);
    const double dy = std::sin(turn);
    const double dz = std::cos(turn) * std::cos(tilt);
    // S lies on the sphere, so the ray meets it again at distance 2 R dz.
    const double distance = 2.0 * g.radius * dz;
    const double s_z = g.centre_z - g.radius;
    return {distance * dx, distance * dy, s_z + distance * dz};
}

bool base_table_value_clamped(const std::array<std::int16_t, 3>& value) {
    for (std::int16_t v : value) {
        if (v >= 32700 || v <= -32700) return true;
    }
    return false;
}

ExtendedBaseGrid::ExtendedBaseGrid(BaseTable table, int ring, bool keep_clamped, GridGeometry geometry)
    : table_(std::move(table)), ring_(ring), keep_clamped_(keep_clamped), geometry_(geometry) {}

std::array<std::int32_t, 3> ExtendedBaseGrid::point(int row, int column) const {
    const bool in_table = row >= 0 && row < base_table_rows && column >= 0 && column < base_table_columns;
    if (in_table) {
        const auto value = table_(row, column);
        if (keep_clamped_ || !base_table_value_clamped(value)) return {value[0], value[1], value[2]};
    }
    const auto p = grid_formula_point(row, column, geometry_);
    return {static_cast<std::int32_t>(std::lround(p[0])), static_cast<std::int32_t>(std::lround(p[1])),
            static_cast<std::int32_t>(std::lround(p[2]))};
}

} // namespace tetrisphere::sphere
