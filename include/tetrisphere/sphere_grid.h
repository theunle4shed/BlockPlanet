#pragma once

// The sphere's base grid of points, at any size (sphere renderer, stage 2).
//
// The game stores a fixed 17 x 18 table of points at 0x800E3CD8 (see
// docs/research/blockplanet-game-state.md, "The base grid"). The points lie
// on a sphere (centre (0, 0, -10764), radius 32856) and form a uniform
// angular grid as seen from the sphere's lowest point S = (0, 0, -43620):
// row r is the plane through S tilted by (8.5 - r) steps about the y
// direction, and column c is (8.5 - c) steps around that row's circle as seen
// from S, with a step of about 5.4532 degrees. The formula reproduces the
// table to within 49 units (0.15%); the table was probably made with a less
// precise sine table.
//
// ExtendedBaseGrid uses the table itself where it has an unclamped value, so
// the original patch is unchanged, and the formula elsewhere.

#include <array>
#include <cstdint>
#include <functional>

namespace tetrisphere::sphere {

struct GridGeometry {
    double centre_z = -10764.013;
    double radius = 32856.216;
    double step_degrees = 5.453236;
    double centre_row = 8.49981;
    double centre_column = 8.5;
};

// The formula's point for (row, column); rows and columns may be fractional
// or outside the table.
std::array<double, 3> grid_formula_point(double row, double column, const GridGeometry& geometry = {});

// The game's table: 17 rows of 18 points.
constexpr int base_table_rows = 17;
constexpr int base_table_columns = 18;
using BaseTable = std::function<std::array<std::int16_t, 3>(int row, int column)>;

// Table values at the 16-bit limit were clamped when the table was made.
bool base_table_value_clamped(const std::array<std::int16_t, 3>& value);

class ExtendedBaseGrid {
public:
    // `ring` extra points on every side of the table.
    ExtendedBaseGrid(BaseTable table, int ring, GridGeometry geometry = {});

    // Row in [-ring, 16 + ring], column in [-ring, 17 + ring].
    std::array<std::int32_t, 3> point(int row, int column) const;
    int ring() const { return ring_; }

private:
    BaseTable table_;
    int ring_;
    GridGeometry geometry_;
};

} // namespace tetrisphere::sphere
