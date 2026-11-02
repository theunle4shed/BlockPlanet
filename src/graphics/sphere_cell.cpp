#include "tetrisphere/sphere_cell.h"

#include <array>

namespace tetrisphere::sphere {
namespace {

// Inputs.
constexpr std::uint32_t cell_row = 0x8015F836u;       // s16, mesh row of the cell
constexpr std::uint32_t cell_column = 0x8015F83Au;    // s16, mesh column of the cell
constexpr std::uint32_t mesh_pointer = 0x800E44E4u;   // 16 x 16 entries, 12 bytes each
constexpr std::uint32_t mesh_row_bytes = 0xC0u;
constexpr std::uint32_t mesh_entry_bytes = 0xCu;
constexpr std::uint32_t corner_shade = 0x8015EED0u;  // 16 per row, 6 bytes each
constexpr std::uint32_t row_shade = 0x8015EDC0u;     // 6 bytes per row
constexpr std::uint32_t column_shade = 0x8015EE20u;  // 6 bytes per column
constexpr std::uint32_t active_context = 0x8013DD00u;
constexpr std::uint32_t board_column_offset = 0x2B20u;  // context word, added to the row
constexpr std::uint32_t board_row_offset = 0x2B24u;     // context word, added to the column
constexpr std::uint32_t board_pointer = 0x800E44DCu;    // column map: one byte per cell, height << 4 | kind
constexpr std::uint32_t board_plane16 = 0x800E44D0u;    // per-cell planes the drawing code reads
constexpr std::uint32_t board_plane8 = 0x800E44D4u;
constexpr std::uint32_t board_plane32 = 0x800E44D8u;

// Outputs.
constexpr std::uint32_t out_cell = 0x8015F842u;  // top block's index in the per-block planes
constexpr std::uint32_t out_kind = 0x8015EDAEu;
constexpr std::uint32_t out_height = 0x8015EDB2u;
constexpr std::uint32_t out_kind_capped = 0x8015D770u;  // byte, set when kind 8 was capped to 7
constexpr std::uint32_t out_plane16 = 0x8015E990u;
constexpr std::uint32_t out_plane8 = 0x8015ED98u;
constexpr std::uint32_t out_plane32 = 0x8015EDA8u;

// Where the drawing code wants each corner, as x, y, z addresses. The layout
// is the game's (fields of the display-list builder's work area).
struct Corner {
    int row;     // 0 or 1, added to the cell row
    int column;  // 0 or 1, added to the cell column
    std::array<std::uint32_t, 3> step;    // copy of the layer step
    std::array<std::uint32_t, 3> top;     // outer face of the top block
    std::array<std::uint32_t, 3> bottom;  // inner face of the top block
};
// In the order the game writes them.
constexpr std::array<Corner, 4> corners{{
    {0, 0, {0x8015F7DCu, 0x8015F7CCu, 0x8015F7EAu}, {0x8015EDA4u, 0x8015ED9Cu, 0x8015EDACu},
     {0x8015F4E8u, 0x8015F4E4u, 0x8015F4EEu}},
    {1, 0, {0x8015F7E8u, 0x8015F7D8u, 0x8015F7F6u}, {0x8015EDB4u, 0x8015EDB0u, 0x8015EDB8u},
     {0x8015F4FEu, 0x8015F4F6u, 0x8015F506u}},
    {1, 1, {0x8015F7E4u, 0x8015F7D4u, 0x8015F7F2u}, {0x8015EE1Au, 0x8015EDBCu, 0x8015EEC8u},
     {0x8015F566u, 0x8015F536u, 0x8015F594u}},
    {0, 1, {0x8015F7E0u, 0x8015F7D0u, 0x8015F7EEu}, {0x8015F4DCu, 0x8015F4D2u, 0x8015F4E0u},
     {0x8015F5A0u, 0x8015F59Au, 0x8015F5A6u}},
}};

// Corner shading values, copied from the table entry at (row, column) and the
// next three entries the game reads: x, y, z addresses per source offset.
struct ShadeCopy {
    std::uint32_t source_offset;  // bytes from the cell's entry
    std::array<std::uint32_t, 3> destination;
};
constexpr std::array<ShadeCopy, 4> corner_shades{{
    {0x00u, {0x8015F7ECu, 0x8015F7FCu, 0x8015F80Cu}},
    {0x60u, {0x8015F7F4u, 0x8015F804u, 0x8015F814u}},
    {0x66u, {0x8015F7F8u, 0x8015F808u, 0x8015F818u}},
    {0x06u, {0x8015F7F0u, 0x8015F800u, 0x8015F810u}},
}};
// Row and column shading: the first vector as is, the second negated.
constexpr std::array<std::uint32_t, 6> row_shade_out{
    0x8015F4D4u, 0x8015F4DEu, 0x8015F4E2u, 0x8015F4E6u, 0x8015F4EAu, 0x8015F4F2u};
constexpr std::array<std::uint32_t, 6> column_shade_out{
    0x8015F4F8u, 0x8015F500u, 0x8015F508u, 0x8015F538u, 0x8015F568u, 0x8015F596u};

std::int16_t s16(std::int32_t v) { return static_cast<std::int16_t>(v); }

// The game indexes 6-byte tables with a 16-bit multiply.
std::uint32_t entry6(std::int32_t index) {
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(s16(index * 3)) * 2);
}

} // namespace

void prepare_cell(GuestMemory& m) {
    const std::int32_t row = m.read16(cell_row);
    const std::int32_t column = m.read16(cell_column);
    const std::uint32_t mesh = m.read32(mesh_pointer);
    auto entry = [&](const Corner& c) {
        return mesh + static_cast<std::uint32_t>((row + c.row) * static_cast<std::int32_t>(mesh_row_bytes) +
                                                 (column + c.column) * static_cast<std::int32_t>(mesh_entry_bytes));
    };

    // 1. Read each corner's base point and layer step; copy the step out.
    std::array<std::array<std::int32_t, 3>, 4> base{};
    std::array<std::array<std::int32_t, 3>, 4> step{};
    for (std::size_t k = 0; k < corners.size(); ++k) {
        const std::uint32_t e = entry(corners[k]);
        for (int axis = 0; axis < 3; ++axis) {
            base[k][axis] = m.read16(e + 2u * static_cast<std::uint32_t>(axis));
            step[k][axis] = m.read16(e + 6u + 2u * static_cast<std::uint32_t>(axis));
        }
    }
    for (std::size_t k = 0; k < corners.size(); ++k)
        for (int axis = 0; axis < 3; ++axis) m.write16(corners[k].step[axis], s16(step[k][axis]));

    // 2. Faces of a one-layer column: top = base - step, bottom = top - step.
    for (std::size_t k = 0; k < corners.size(); ++k)
        for (int axis = 0; axis < 3; ++axis) {
            const std::int16_t top = s16(base[k][axis] - step[k][axis]);
            m.write16(corners[k].bottom[axis], top);
            m.write16(corners[k].top[axis], top);
        }
    for (std::size_t k = 0; k < corners.size(); ++k)
        for (int axis = 0; axis < 3; ++axis)
            m.write16(corners[k].bottom[axis], s16(base[k][axis] - 2 * step[k][axis]));

    // 3. Shading values for the corners, the row and the column.
    const std::uint32_t shade = corner_shade + entry6(row * 16 + column);
    for (const ShadeCopy& copy : corner_shades)
        for (int axis = 0; axis < 3; ++axis)
            m.write16(copy.destination[axis],
                      m.read16(shade + copy.source_offset + 2u * static_cast<std::uint32_t>(axis)));
    const std::uint32_t row_entry = row_shade + entry6(row);
    const std::uint32_t column_entry = column_shade + entry6(column);
    for (int n = 0; n < 6; ++n) {
        const std::int16_t r = m.read16(row_entry + 2u * static_cast<std::uint32_t>(n));
        m.write16(row_shade_out[n], n < 3 ? r : s16(-r));
    }
    for (int n = 0; n < 6; ++n) {
        const std::int16_t c = m.read16(column_entry + 2u * static_cast<std::uint32_t>(n));
        m.write16(column_shade_out[n], n < 3 ? c : s16(-c));
    }

    // 4. The board cell under this grid cell (the board is 32 x 32 and scrolls).
    const std::uint32_t context = m.read32(active_context);
    const std::int32_t board_x = static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(row) + m.read32(context + board_column_offset)) & 0x1Fu);
    const std::int32_t board_y = static_cast<std::int32_t>(
        (static_cast<std::uint32_t>(column) + m.read32(context + board_row_offset)) & 0x1Fu);
    const std::int32_t cell = (board_y << 5) + board_x;
    m.write16(out_cell, s16(cell - 1024));  // the game stores it biased first
    const std::uint8_t packed = m.read8(m.read32(board_pointer) + static_cast<std::uint32_t>(cell));
    m.write16(out_kind, packed);
    std::int16_t height = static_cast<std::int16_t>(packed >> 4);
    m.write16(out_height, height);
    if (height == 0) {
        height = 1;
        m.write16(out_height, height);
    }
    std::int16_t kind = static_cast<std::int16_t>(packed & 0xF);
    m.write16(out_kind, kind);
    kind = s16(kind + 1);
    m.write16(out_kind, kind);
    if (kind == 8) {
        m.write16(out_kind, 7);
        m.write8(out_kind_capped, 1);
    }

    // 5. Raise both faces to the top of the column: + (height - 1) layers on
    // top of the one-layer faces written above, i.e. + height steps here.
    const std::int32_t layers = height & 0xFF;
    for (std::size_t k = 0; k < corners.size(); ++k)
        for (int axis = 0; axis < 3; ++axis) {
            const std::int32_t lift = step[k][axis] * layers;
            m.write16(corners[k].top[axis], s16(m.read16(corners[k].top[axis]) + lift));
        }
    for (std::size_t k = 0; k < corners.size(); ++k)
        for (int axis = 0; axis < 3; ++axis) {
            const std::int32_t lift = step[k][axis] * layers;
            m.write16(corners[k].bottom[axis], s16(m.read16(corners[k].bottom[axis]) + lift));
        }

    // 6. Point the drawing code at the top block's entries in the per-block
    // planes, which hold one 32 x 32 layer after another (1024 cells each).
    const std::int16_t index = s16(m.read16(out_cell) + (height << 10));
    m.write16(out_cell, index);
    m.write32(out_plane16, static_cast<std::uint32_t>(index) * 2u + m.read32(board_plane16));
    m.write32(out_plane8, static_cast<std::uint32_t>(index) + m.read32(board_plane8));
    m.write32(out_plane32, static_cast<std::uint32_t>(index) * 4u + m.read32(board_plane32));
}

} // namespace tetrisphere::sphere
