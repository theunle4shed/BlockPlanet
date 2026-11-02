#pragma once

// BlockPlanet's reimplementation of the sphere mesh table builder
// (originally func_800ADD6C). Stage 1 of the sphere renderer: produce exactly
// what the original produces, with the grid size as a parameter so later
// stages can draw more of the sphere. See docs/research/blockplanet-game-state.md.

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace tetrisphere::sphere {

// One table entry, as the game stores it: the outer surface point (direction
// / 3) and the core point (direction / 10).
struct MeshEntry {
    std::int16_t outer[3];
    std::int16_t inner[3];
};

struct MeshInputs {
    // Base grid of unit directions (s16, 1.0 = 0x7FFF), row-major,
    // `base_columns` points per row. The game's grid has 18 columns.
    // Rows run from 0 to `rows`, columns from 0 to `columns` + 1 (in the
    // extended grid these are offset so that 0 is the first extra row).
    std::function<std::array<std::int32_t, 3>(int row, int column)> base;
    // Sub-cell rotation, as passed in a0 and a1 (s16).
    std::int16_t row_fraction = 0;
    std::int16_t column_fraction = 0;
    // Optional per-axis scale (context +0x2478 flag, +0x247A/C/E values,
    // 0x4000 = 1.0), already capped at 0x4000.
    bool scale_enabled = false;
    std::array<std::int16_t, 3> scale{0x4000, 0x4000, 0x4000};
    // Two-player offset (0x8015D980 >= 2): +0x40 for player 1, -0x20 otherwise.
    bool two_player = false;
    bool player_one = true;
    int rows = 16;
    int columns = 16;
    // The game keeps intermediate values in 16 bits. Extended grids have
    // points beyond that range, so `wide` keeps them in 32 bits; with values
    // inside the 16-bit range the result is the same.
    bool wide = false;
};

// Fill `out` (rows x columns entries) exactly as func_800ADD6C does.
void build_mesh(const MeshInputs& inputs, std::vector<MeshEntry>& out);

// Hook entry points, called from the generated game code (see
// config/recomp/tetrisphere.us.rev0.toml). Mode is BLOCKPLANET_SPHERE:
// original (default), verify, or replace.
enum class Mode { Original, Verify, Replace };
Mode mode();

} // namespace tetrisphere::sphere
