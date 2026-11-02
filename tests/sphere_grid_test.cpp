#include "tetrisphere/sphere_grid.h"

#include <cassert>
#include <cmath>

using namespace tetrisphere::sphere;

namespace {

double distance(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

double angle_from_s(const std::array<double, 3>& a, const std::array<double, 3>& b, const GridGeometry& g) {
    const std::array<double, 3> s{0, 0, g.centre_z - g.radius};
    double dot = 0, la = 0, lb = 0;
    for (int i = 0; i < 3; ++i) {
        dot += (a[i] - s[i]) * (b[i] - s[i]);
        la += (a[i] - s[i]) * (a[i] - s[i]);
        lb += (b[i] - s[i]) * (b[i] - s[i]);
    }
    return std::acos(dot / std::sqrt(la * lb)) * 180.0 / 3.14159265358979323846;
}

} // namespace

int main() {
    const GridGeometry g;
    const std::array<double, 3> centre{0, 0, g.centre_z};
    // Every point, inside the table or far outside it, is on the sphere.
    for (double r : {-4.0, 0.0, 8.5, 16.0, 20.0}) {
        for (double c : {-4.0, 0.0, 8.5, 17.0, 21.0}) {
            assert(std::fabs(distance(grid_formula_point(r, c), centre) - g.radius) < 1e-6);
        }
    }
    // The middle of the grid faces the camera (+z).
    const auto middle = grid_formula_point(g.centre_row, g.centre_column);
    assert(std::fabs(middle[0]) < 1e-6 && std::fabs(middle[1]) < 1e-6 && middle[2] > 0);
    // Neighbours along the middle row and column are one step apart as seen from S.
    assert(std::fabs(angle_from_s(grid_formula_point(8.5, 3), grid_formula_point(8.5, 4), g) - g.step_degrees) < 1e-6);
    assert(std::fabs(angle_from_s(grid_formula_point(3, 8.5), grid_formula_point(4, 8.5), g) - g.step_degrees) < 1e-6);

    // The extended grid takes table values where they are not clamped.
    const BaseTable table = [](int row, int column) {
        if (row == 0 && column == 8) return std::array<std::int16_t, 3>{32767, 5, 6};  // clamped
        return std::array<std::int16_t, 3>{static_cast<std::int16_t>(row), static_cast<std::int16_t>(column), 7};
    };
    const ExtendedBaseGrid grid(table, 2);
    assert((grid.point(3, 4) == std::array<std::int32_t, 3>{3, 4, 7}));
    const auto formula = grid_formula_point(0, 8);
    assert(grid.point(0, 8)[0] == static_cast<std::int32_t>(std::lround(formula[0])));
    const auto outside = grid_formula_point(-2, 19);
    assert(grid.point(-2, 19)[1] == static_cast<std::int32_t>(std::lround(outside[1])));
    return 0;
}
